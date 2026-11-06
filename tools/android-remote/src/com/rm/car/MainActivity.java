package com.rm.car;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.Context;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.LinkedBlockingQueue;

/**
 * RM infantry car remote, bluetooth SPP (HC-05 / JDY31), two-stick layout.
 *
 *   left stick  : translation (push forward = 前进, left = 横移)
 *   right stick : gimbal (left/right = 偏航, up/down = 俯仰)
 *   centre      : STOP / FIRE
 *   bottom      : speed preset, status query, handshake, protocol switch
 *
 * Protocol RM (Mecanum-Car v3.6):
 *   v vx vy wz  velocity, -1000..1000, resent every 50 ms while a stick is off centre
 *   x           stop
 *   y deg10     gimbal yaw 0..1800
 *   s deg10     gimbal pitch 100..1700  (smaller = 抬头)
 *   f / f1      fire burst 3 / single
 *   g           status report  -> "gimbal yaw=90.0 pitch=60.0 homing=0 follow=0"
 *   p           ping -> "RM-INFANTRY-CAR"
 *   follow 0|1  chassis follows gimbal yaw
 *
 * Protocol RS05 (@): @PING @ARM @F @B @L @R @FL @FR @BL @BR @S @! @SPEED=n
 */
public class MainActivity extends Activity {

    private static final UUID SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final int MOTION_MS = 50;
    private static final int GIMBAL_MS = 60;
    private static final int STATUS_MS = 2000;
    private static final int REQ_PERM = 1;
    private static final int PITCH_MIN = 100;
    private static final int PITCH_MAX = 1700;
    private static final int YAW_MIN = 0;
    private static final int YAW_MAX = 1800;
    private static final int PROTO_RM = 0;
    private static final int PROTO_RS05 = 1;
    private static final String PREFS = "rm_car";
    private static final String PREF_MAC = "last_mac";

    private static final int C_BG = Color.parseColor("#0d1318");
    private static final int C_TEXT = Color.parseColor("#e6eef4");
    private static final int C_DRIVE = Color.parseColor("#39c0ed");
    private static final int C_GIMBAL = Color.parseColor("#f0a341");

    private BluetoothAdapter adapter;
    private BluetoothSocket socket;
    private volatile OutputStream out;
    private Thread readThread, writeThread;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Handler motion = new Handler(Looper.getMainLooper());
    private final LinkedBlockingQueue<String> txQueue = new LinkedBlockingQueue<String>();
    private final StringBuilder logText = new StringBuilder();

    private TextView statusView, rxView, logView, speedView;
    private Spinner deviceSpinner;
    private final List<BluetoothDevice> devices = new ArrayList<BluetoothDevice>();

    private int proto = PROTO_RM;
    private int speed = 600;
    private int yaw10 = 900;
    private int pitch10 = 900;
    private boolean followOn;

    private volatile int stickVx, stickVy, stickWz;
    private boolean moving;
    private long lastGimbalSend;
    private String holdCmd;

    // ---------------------------------------------------------------- lifecycle

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        adapter = BluetoothAdapter.getDefaultAdapter();
        setContentView(buildUi());
        startWriteThread();
        ensurePermissions();
        motion.postDelayed(motionTick, MOTION_MS);
        ui.postDelayed(statusTick, STATUS_MS);
    }

    @Override
    protected void onDestroy() {
        motion.removeCallbacks(motionTick);
        motion.removeCallbacks(holdTick);
        ui.removeCallbacks(statusTick);
        disconnect();
        super.onDestroy();
    }

    // ---------------------------------------------------------------- UI

    private View buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(C_BG);
        root.setPadding(dp(6), dp(4), dp(6), dp(4));

        root.addView(topBar());
        root.addView(mainArea(), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        root.addView(bottomBar());

        logView = new TextView(this);
        logView.setTextSize(8);
        logView.setTypeface(Typeface.MONOSPACE);
        logView.setTextColor(Color.parseColor("#5b6d79"));
        logView.setText(logText.toString());
        logView.setMaxLines(2);
        root.addView(logView, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(26)));
        return root;
    }

    /** 第一行：链路状态 + 车回传 + 选设备 + 连接/断开/说明。 */
    private View topBar() {
        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.HORIZONTAL);
        top.setGravity(Gravity.CENTER_VERTICAL);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        statusView = new TextView(this);
        statusView.setText("未连接");
        statusView.setTextSize(13);
        statusView.setTextColor(C_TEXT);
        statusView.setTypeface(Typeface.DEFAULT_BOLD);
        col.addView(statusView);
        rxView = new TextView(this);
        rxView.setText("车回传：-");
        rxView.setTextSize(10);
        rxView.setTypeface(Typeface.MONOSPACE);
        rxView.setTextColor(Color.parseColor("#8fd6a0"));
        col.addView(rxView);
        top.addView(col, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        deviceSpinner = new Spinner(this);
        top.addView(deviceSpinner, new LinearLayout.LayoutParams(dp(130), dp(40)));

        top.addView(barButton("连接", new Runnable() { public void run() { connect(); } }), fixed(dp(62), dp(40)));
        top.addView(barButton("断开", new Runnable() { public void run() { disconnect(); } }), fixed(dp(62), dp(40)));
        top.addView(barButton("说明", new Runnable() { public void run() { showHelp(); } }), fixed(dp(58), dp(40)));
        return top;
    }

    /** 中间：左=行驶摇杆，中=停车/发射，右=云台摇杆。 */
    private View mainArea() {
        LinearLayout main = new LinearLayout(this);
        main.setOrientation(LinearLayout.HORIZONTAL);

        // ---------- 左：行驶
        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.addView(hint("行驶摇杆：推前=前进 拉后=倒退 左右=平移", C_DRIVE));
        StickView drive = new StickView(this, true, true, "推杆开车", C_DRIVE,
                new StickView.Listener() {
                    public void onVec(float nx, float ny) {
                        stickVx = Math.round(-ny * speed);
                        stickVy = Math.round(-nx * speed);
                    }
                });
        left.addView(drive, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout rotRow = new LinearLayout(this);
        rotRow.setOrientation(LinearLayout.HORIZONTAL);
        rotRow.addView(holdButton("左转 CCW", null, false, +1), cell(1f, dp(46)));
        rotRow.addView(holdButton("右转 CW", null, false, -1), cell(1f, dp(46)));
        left.addView(rotRow);
        main.addView(left, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1.15f));

        // ---------- 中：停车 / 发射
        LinearLayout mid = new LinearLayout(this);
        mid.setOrientation(LinearLayout.VERTICAL);

        Button stop = new Button(this);
        stop.setText("停 车");
        stop.setTextSize(20);
        stop.setTextColor(Color.WHITE);
        stop.setAllCaps(false);
        stop.setBackgroundColor(Color.parseColor("#7a2b22"));
        stop.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { stopEverything(true); }
        });
        mid.addView(stop, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.1f));

        Button fire = new Button(this);
        fire.setText("发 射");
        fire.setTextSize(19);
        fire.setAllCaps(false);
        fire.setBackgroundColor(Color.parseColor("#245c3c"));
        fire.setTextColor(Color.WHITE);
        fire.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { sendLine("f"); }
        });
        mid.addView(fire, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 0.95f));

        mid.addView(clickButton("单发", new Cmd() { public String get() { return "f1"; } }),
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(42)));
        main.addView(mid, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 0.78f));

        // ---------- 右：云台
        LinearLayout right = new LinearLayout(this);
        right.setOrientation(LinearLayout.VERTICAL);
        right.addView(hint("云台摇杆：左右=偏航 上下=俯仰(上=抬头)", C_GIMBAL));
        StickView gim = new StickView(this, true, true, "推杆瞄准", C_GIMBAL,
                new StickView.Listener() {
                    public void onVec(float nx, float ny) {
                        if (nx != 0f) setYaw(900 + Math.round(nx * 800));
                        if (ny != 0f) setPitch(900 + Math.round(ny * 800));
                    }
                });
        right.addView(gim, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout aimRow = new LinearLayout(this);
        aimRow.setOrientation(LinearLayout.HORIZONTAL);
        aimRow.addView(clickButton("云台回中", new Cmd() {
            public String get() {
                yaw10 = 900;
                pitch10 = 900;
                sendLine("s 900");
                return "y 900";
            }
        }), cell(1f, dp(46)));
        aimRow.addView(barButton("跟随", new Runnable() { public void run() { toggleFollow(); } }), cell(1f, dp(46)));
        right.addView(aimRow);
        main.addView(right, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1.15f));
        return main;
    }

    /** @ 协议那套固件只有离散方向指令，用 8 个方向键 + 使能/失能。 */
    private View rs05Area() {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.addView(hint("@ 模式：按住方向持续行驶，松手自动 @S；先 PING，再 ARM，再走", C_GIMBAL));
        col.addView(dirRow(new String[] { "左前", "前", "右前" }, new String[] { "@FL", "@F", "@FR" }), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        col.addView(dirRow(new String[] { "左转", "@S 停", "右转" }, new String[] { "@L", "@S", "@R" }), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        col.addView(dirRow(new String[] { "左后", "后", "右后" }, new String[] { "@BL", "@B", "@BR" }), new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        LinearLayout arm = new LinearLayout(this);
        arm.setOrientation(LinearLayout.HORIZONTAL);
        arm.addView(clickButton("PING", new Cmd() { public String get() { return "@PING"; } }), cell(1f, dp(48)));
        arm.addView(clickButton("ARM 使能", new Cmd() { public String get() { return "@ARM"; } }), cell(1f, dp(48)));
        arm.addView(clickButton("DISARM", new Cmd() { public String get() { return "@!"; } }), cell(1f, dp(48)));
        col.addView(arm);
        return col;
    }

    private View dirRow(String[] labels, final String[] cmds) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        for (int i = 0; i < labels.length; i++) {
            final String c = cmds[i];
            r.addView(holdButton(labels[i], new Cmd() { public String get() { return c; } }, true, 0), cell(1f, dp(56)));
        }
        return r;
    }
    /** 第三行：速度档 + 状态 + 协议切换。 */
    private View bottomBar() {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);

        speedView = new TextView(this);
        speedView.setText("速度 " + speed);
        speedView.setTextSize(11);
        speedView.setTextColor(C_TEXT);
        bar.addView(speedView, new LinearLayout.LayoutParams(dp(58), dp(40)));
        bar.addView(barButton("慢 300", new Runnable() { public void run() { setSpeed(300); } }), fixed(dp(64), dp(40)));
        bar.addView(barButton("中 600", new Runnable() { public void run() { setSpeed(600); } }), fixed(dp(64), dp(40)));
        bar.addView(barButton("快 1000", new Runnable() { public void run() { setSpeed(1000); } }), fixed(dp(70), dp(40)));
        bar.addView(barButton("查状态", new Runnable() { public void run() { sendLine("g"); } }), fixed(dp(64), dp(40)));
        bar.addView(barButton("握手", new Runnable() {
            public void run() { sendLine(proto == PROTO_RM ? "p" : "@PING"); }
        }), fixed(dp(54), dp(40)));

        View spacer = new View(this);
        bar.addView(spacer, new LinearLayout.LayoutParams(0, dp(1), 1f));

        Button protoBtn = new Button(this);
        protoBtn.setText(proto == PROTO_RM ? "协议 RM" : "协议 @");
        protoBtn.setTextSize(11);
        protoBtn.setAllCaps(false);
        protoBtn.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                stopEverything(true);
                proto = (proto == PROTO_RM) ? PROTO_RS05 : PROTO_RM;
                setContentView(buildUi());
                loadDevices();
            }
        });
        bar.addView(protoBtn, fixed(dp(78), dp(40)));
        return bar;
    }

    private TextView hint(String text, int color) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(10);
        t.setTextColor(color);
        t.setPadding(dp(4), dp(2), dp(4), dp(2));
        return t;
    }

    private void showHelp() {
        String msg =
                "左手【行驶摇杆】推前=前进，拉后=倒退，左右=横向平移（麦轮），推得多快就走多快。\n" +
                "【左转/右转】按住一直原地转，可以同时推摇杆，边走边转。\n\n" +
                "右手【云台摇杆】左右=偏航（炮塔方向），上下=俯仰，往上推是抬头。\n" +
                "【云台回中】偏航+俯仰一起回到 90 度。\n" +
                "【跟随】底盘跟着云台方向走（需要底盘角度环，默认关）。\n\n" +
                "中间【停车】摇杆归零并立刻刹停，任何异常先按它。\n" +
                "【发射】三连发（水弹 AEG 单发会卡在半压缩位）。\n" +
                "【单发】只打一发，用于点射。\n\n" +
                "底部【慢/中/快】摇杆最大输出 300/600/1000，上场先慢后快。\n" +
                "【查状态】读一次云台角度，显示在左上“车回传”。\n" +
                "【握手】确认链路，RM 回 RM-INFANTRY-CAR。\n" +
                "【协议】本仓库固件选 RM；@ 那套桥接固件选 @。\n\n" +
                "顶部第一行显示：连接状态、车上回传的偏航/俯仰真值。";
        new AlertDialog.Builder(this)
                .setTitle("按键说明")
                .setMessage(msg)
                .setPositiveButton("知道了", new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface d, int w) { d.dismiss(); }
                }).show();
    }

    private LinearLayout.LayoutParams fixed(int w, int h) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(w, h);
        p.setMargins(dp(2), 0, 0, 0);
        return p;
    }

    private LinearLayout.LayoutParams cell(float weight, int h) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, h, weight);
        p.setMargins(dp(2), dp(2), dp(2), 0);
        return p;
    }

    private Button barButton(String label, final Runnable action) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextSize(11);
        b.setAllCaps(false);
        b.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { action.run(); }
        });
        return b;
    }

    private Button base(String label) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextSize(12);
        b.setAllCaps(false);
        return b;
    }

    private View clickButton(String label, final Cmd cmd) {
        Button b = base(label);
        b.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                String s = cmd.get();
                if (s != null) sendLine(s);
            }
        });
        return b;
    }

    private View holdButton(String label, final Cmd cmd, final boolean isMotion, final int rotDir) {
        Button b = base(label);
        b.setOnTouchListener(new View.OnTouchListener() {
            public boolean onTouch(View v, MotionEvent e) {
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        if (rotDir != 0) {
                            stickWz = rotDir * speed;
                            sendMotion();
                        } else if (cmd != null) {
                            startHold(cmd.get());
                        }
                        v.setPressed(true);
                        return true;
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        if (rotDir != 0) {
                            stickWz = 0;
                            sendMotion();
                        } else {
                            stopHold(isMotion);
                        }
                        v.setPressed(false);
                        return true;
                    default:
                        return false;
                }
            }
        });
        return b;
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    // ------------------------------------------------------------ command state

    private interface Cmd { String get(); }

    private void setSpeed(int v) {
        speed = v;
        if (speedView != null) speedView.setText("速度 " + speed);
    }

    /** Held buttons keep resending: the firmware drops motion after 500 ms of silence. */
    private final Runnable holdTick = new Runnable() {
        public void run() {
            if (holdCmd != null) {
                sendLine(holdCmd);
                motion.postDelayed(this, 200);
            }
        }
    };

    private void startHold(String cmd) {
        if (cmd == null) return;
        holdCmd = cmd;
        sendLine(cmd);
        motion.removeCallbacks(holdTick);
        motion.postDelayed(holdTick, 200);
    }

    private void stopHold(boolean wasMotion) {
        holdCmd = null;
        motion.removeCallbacks(holdTick);
        if (wasMotion && proto == PROTO_RS05) sendLine("@S");
    }

    private void stopEverything(boolean sendFrame) {
        stickVx = 0;
        stickVy = 0;
        stickWz = 0;
        moving = false;
        stopHold(false);
        if (sendFrame) sendLine(proto == PROTO_RM ? "x" : "@S");
    }

    private void setYaw(int v) {
        if (v < YAW_MIN) v = YAW_MIN;
        if (v > YAW_MAX) v = YAW_MAX;
        if (v == yaw10) return;
        yaw10 = v;
        sendGimbal("y " + yaw10);
    }

    private void setPitch(int v) {
        if (v < PITCH_MIN) v = PITCH_MIN;
        if (v > PITCH_MAX) v = PITCH_MAX;
        if (v == pitch10) return;
        pitch10 = v;
        sendGimbal("s " + pitch10);
    }

    private void sendGimbal(String s) {
        long now = SystemClock.uptimeMillis();
        if (now - lastGimbalSend < GIMBAL_MS) return;
        lastGimbalSend = now;
        sendLine(s);
    }

    private void sendMotion() {
        if (proto != PROTO_RM) return;
        int vx = stickVx, vy = stickVy, wz = stickWz;
        if (vx == 0 && vy == 0 && wz == 0) {
            if (moving) { sendLine("x"); moving = false; }
            return;
        }
        moving = true;
        sendLine("v " + vx + " " + vy + " " + wz);
    }

    private void toggleFollow() {
        followOn = !followOn;
        sendLine("follow " + (followOn ? "1" : "0"));
        if (statusView != null) statusView.setText(followOn ? "跟随开" : "跟随关");
    }

    /** 50 ms heartbeat: keeps the velocity target alive while a stick is off centre. */
    private final Runnable motionTick = new Runnable() {
        public void run() {
            sendMotion();
            motion.postDelayed(this, MOTION_MS);
        }
    };

    /** Polls status so the operator sees the link is alive and where the gimbal sits. */
    private final Runnable statusTick = new Runnable() {
        public void run() {
            if (out != null && proto == PROTO_RM) sendLine("g");
            ui.postDelayed(this, STATUS_MS);
        }
    };

    // ------------------------------------------------------------ serial io

    private void startWriteThread() {
        writeThread = new Thread(new Runnable() {
            public void run() {
                while (true) {
                    try {
                        String s = txQueue.take();
                        OutputStream o = out;
                        if (o == null) continue;
                        o.write((s + "\r\n").getBytes("UTF-8"));
                        o.flush();
                    } catch (InterruptedException ie) {
                        return;
                    } catch (Exception e) {
                        logLine("TX error: " + e);
                    }
                }
            }
        });
        writeThread.setDaemon(true);
        writeThread.start();
    }

    private void sendLine(String s) {
        if (s == null || s.length() == 0) return;
        if (out == null) return;
        txQueue.offer(s);
    }

    private void connect() {
        int idx = deviceSpinner == null ? -1 : deviceSpinner.getSelectedItemPosition();
        if (idx < 0 || idx >= devices.size()) { logLine("select a paired device first"); return; }
        if (adapter != null && !adapter.isEnabled()) { logLine("bluetooth is off"); return; }
        final BluetoothDevice dev = devices.get(idx);
        statusView.setText("正在连接 " + dev.getName() + " ...");
        logLine("connecting " + dev.getAddress());
        new Thread(new Runnable() {
            public void run() {
                try {
                    if (adapter != null) adapter.cancelDiscovery();
                    BluetoothSocket s = dev.createRfcommSocketToServiceRecord(SPP_UUID);
                    try {
                        s.connect();
                    } catch (Exception e1) {
                        try { s.close(); } catch (Exception ignore) { }
                        Method m = dev.getClass().getMethod("createRfcommSocket", new Class[] { int.class });
                        s = (BluetoothSocket) m.invoke(dev, Integer.valueOf(1));
                        s.connect();
                    }
                    socket = s;
                    out = s.getOutputStream();
                    final String name = dev.getName();
                    prefs().edit().putString(PREF_MAC, dev.getAddress()).apply();
                    ui.post(new Runnable() {
                        public void run() { statusView.setText("已连接：" + name); }
                    });
                    logLine("connected");
                    sendLine(proto == PROTO_RM ? "p" : "@PING");
                    startReadThread(s);
                } catch (final Exception e) {
                    out = null;
                    ui.post(new Runnable() {
                        public void run() { statusView.setText("连接失败"); }
                    });
                    logLine("connect failed: " + e);
                }
            }
        }).start();
    }

    private void startReadThread(final BluetoothSocket s) {
        if (readThread != null) readThread.interrupt();
        readThread = new Thread(new Runnable() {
            public void run() {
                try {
                    BufferedReader r = new BufferedReader(new InputStreamReader(s.getInputStream()));
                    String line;
                    while ((line = r.readLine()) != null) {
                        logLine("RX " + line);
                        final String t = line.trim();
                        ui.post(new Runnable() {
                            public void run() { onRxLine(t); }
                        });
                    }
                } catch (Exception e) {
                    logLine("reader stopped: " + e.getMessage());
                    ui.post(new Runnable() {
                        public void run() { statusView.setText("链路断开"); }
                    });
                }
            }
        });
        readThread.setDaemon(true);
        readThread.start();
    }

    private void onRxLine(String t) {
        if (rxView != null) rxView.setText("车回传：" + (t.length() > 44 ? t.substring(0, 44) : t));
        if (t.indexOf("RM-INFANTRY-CAR") >= 0) statusView.setText("车已就绪");
        else if (t.startsWith("PONG")) statusView.setText("已就绪 " + t);
        else if (t.indexOf("yaw=") >= 0) {
            Double y = numberAfter(t, "yaw=");
            Double p = numberAfter(t, "pitch=");
            if (y != null) yaw10 = (int) Math.round(y.doubleValue() * 10.0);
            if (p != null) pitch10 = (int) Math.round(p.doubleValue() * 10.0);
            Double f = numberAfter(t, "follow=");
            if (f != null) followOn = f.doubleValue() != 0.0;
            statusView.setText("偏航 " + (yaw10 / 10) + "°  俯仰 " + (pitch10 / 10) + "°"
                    + (followOn ? "  跟随中" : ""));
        }
    }

    private Double numberAfter(String s, String key) {
        int i = s.indexOf(key);
        if (i < 0) return null;
        int j = i + key.length();
        int k = j;
        while (k < s.length()) {
            char c = s.charAt(k);
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') k++;
            else break;
        }
        if (k == j) return null;
        try { return Double.valueOf(s.substring(j, k)); } catch (Exception e) { return null; }
    }

    private void disconnect() {
        stopEverything(false);
        out = null;
        try { if (socket != null) socket.close(); } catch (Exception ignore) { }
        socket = null;
        if (readThread != null) {
            readThread.interrupt();
            readThread = null;
        }
        if (statusView != null) statusView.setText("未连接");
        logLine("disconnected");
    }

    private void logLine(final String s) {
        ui.post(new Runnable() {
            public void run() {
                logText.append(s).append('\n');
                if (logText.length() > 2000) logText.delete(0, logText.length() - 1700);
                if (logView != null) logView.setText(logText.toString());
            }
        });
    }

    private SharedPreferences prefs() {
        return getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    // ------------------------------------------------------------ permissions

    private void ensurePermissions() {
        if (Build.VERSION.SDK_INT >= 31) {
            List<String> need = new ArrayList<String>();
            if (checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED)
                need.add(Manifest.permission.BLUETOOTH_CONNECT);
            if (checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED)
                need.add(Manifest.permission.BLUETOOTH_SCAN);
            if (!need.isEmpty()) {
                requestPermissions(need.toArray(new String[need.size()]), REQ_PERM);
                return;
            }
        }
        loadDevices();
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        loadDevices();
    }

    private void loadDevices() {
        devices.clear();
        List<String> names = new ArrayList<String>();
        if (adapter == null) {
            names.add("no bluetooth");
        } else {
            try {
                if (!adapter.isEnabled()) {
                    names.add("bluetooth off");
                } else {
                    String lastMac = prefs().getString(PREF_MAC, "");
                    int sel = -1;
                    for (BluetoothDevice d : adapter.getBondedDevices()) {
                        if (lastMac != null && lastMac.equalsIgnoreCase(d.getAddress())) sel = devices.size();
                        devices.add(d);
                        String n = d.getName();
                        names.add((n == null ? "?" : n));
                    }
                    if (devices.isEmpty()) names.add("no paired device");
                    if (sel >= 0 && deviceSpinner != null) deviceSpinner.setSelection(sel);
                }
            } catch (SecurityException se) {
                names.add("no permission");
            }
        }
        if (deviceSpinner != null) {
            deviceSpinner.setAdapter(new ArrayAdapter<String>(this,
                    android.R.layout.simple_spinner_dropdown_item, names));
        }
    }

    // ------------------------------------------------------------ stick widget

    /** Self-drawn thumb stick: round base, draggable knob, one or two axes. */
    static class StickView extends View {
        interface Listener { void onVec(float nx, float ny); }

        private final Listener listener;
        private final boolean useX, useY;
        private final String label;
        private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint knobPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private float nx, ny;

        StickView(Context c, boolean useX, boolean useY, String label, int accent, Listener l) {
            super(c);
            this.useX = useX;
            this.useY = useY;
            this.label = label;
            this.listener = l;
            ring.setStyle(Paint.Style.STROKE);
            ring.setStrokeWidth(3f);
            ring.setColor(Color.parseColor("#324450"));
            knobPaint.setColor(accent);
            textPaint.setColor(Color.parseColor("#6f8593"));
            textPaint.setTextSize(20f);
            textPaint.setTextAlign(Paint.Align.CENTER);
        }

        @Override
        protected void onDraw(Canvas cv) {
            float w = getWidth(), h = getHeight();
            float cx = w / 2f, cy = h / 2f - 8f;
            float r = Math.min(w, h) * 0.5f - 14f;
            if (r < 30f) r = 30f;
            cv.drawCircle(cx, cy, r, ring);
            cv.drawLine(cx - r * 0.45f, cy, cx + r * 0.45f, cy, ring);
            cv.drawLine(cx, cy - r * 0.45f, cx, cy + r * 0.45f, ring);
            cv.drawCircle(cx + nx * r * 0.7f, cy + ny * r * 0.7f, r * 0.28f, knobPaint);
            cv.drawText(label, cx, cy + r + 16f, textPaint);
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            float w = getWidth(), h = getHeight();
            float cx = w / 2f, cy = h / 2f - 8f;
            float r = Math.min(w, h) * 0.5f - 14f;
            if (r < 30f) r = 30f;
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_MOVE: {
                    float dx = (e.getX() - cx) / (r * 0.7f);
                    float dy = (e.getY() - cy) / (r * 0.7f);
                    if (!useX) dx = 0;
                    if (!useY) dy = 0;
                    float m = (float) Math.sqrt(dx * dx + dy * dy);
                    if (m > 1f) { dx /= m; dy /= m; }
                    nx = dx;
                    ny = dy;
                    if (listener != null) listener.onVec(nx, ny);
                    invalidate();
                    return true;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL: {
                    nx = 0;
                    ny = 0;
                    if (listener != null) listener.onVec(0, 0);
                    invalidate();
                    return true;
                }
                default:
                    return false;
            }
        }
    }
}