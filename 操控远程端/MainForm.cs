using System.Drawing.Drawing2D;
using System.IO.Ports;

namespace RMRemote;

internal sealed class MainForm : Form
{
    private static readonly Color Back = Color.FromArgb(17, 23, 24);
    private static readonly Color Panel = Color.FromArgb(30, 39, 40);
    private static readonly Color Raised = Color.FromArgb(41, 52, 53);
    private static readonly Color Ink = Color.FromArgb(235, 241, 238);
    private static readonly Color Muted = Color.FromArgb(151, 168, 163);
    private static readonly Color Mint = Color.FromArgb(67, 210, 162);
    private static readonly Color Amber = Color.FromArgb(244, 180, 76);
    private static readonly Color Red = Color.FromArgb(239, 91, 91);

    private readonly ComboBox ports = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 145 };
    private readonly Button connectButton = new() { Text = "连接", Width = 92, Height = 36 };
    private readonly Button refreshButton = new() { Text = "刷新串口", Width = 100, Height = 36 };
    private readonly CheckBox dryMode = new() { Text = "离线演练", Checked = true, AutoSize = true };
    private readonly Label connectionState = new() { Text = "离线演练", AutoSize = true };
    private readonly Label motionState = new() { Text = "静止", AutoSize = true };
    private readonly Label speedState = new() { Text = "低速  300", AutoSize = true };
    private readonly Label yawState = new() { Text = "Yaw  90.0°", AutoSize = true };
    private readonly Label pitchState = new() { Text = "Pitch  90.0°", AutoSize = true };
    private readonly Label telemetryState = new() { Text = "遥测等待连接", AutoSize = true };
    private readonly Label pongState = new() { Text = "固件未连接", AutoSize = true };
    private readonly Label mouseHint = new() { Text = "按住此区域右键并移动鼠标，调整云台", AutoSize = true };
    private readonly Label vectorState = new() { Text = "VX 0    VY 0    WZ 0", AutoSize = true };
    private readonly RichTextBox log = new() { ReadOnly = true, BorderStyle = BorderStyle.None, DetectUrls = false };
    private readonly Panel aimSurface = new();
    private readonly HashSet<Keys> down = [];
    private readonly object serialGate = new();
    private readonly object inputGate = new();
    private SerialPort? serial;
    private CancellationTokenSource? pumpStop;
    private Task? pumpTask;
    private RmProtocol protocol = new();
    private int yaw10 = 900;
    private int pitch10 = 900;
    private DateTime lastPong = DateTime.MinValue;
    private DateTime lastPing = DateTime.MinValue;
    private Point lastMouse;
    private bool aiming;
    private bool closing;

    public MainForm()
    {
        Text = "RM 步兵战车控制台";
        MinimumSize = new Size(980, 680);
        Size = new Size(1180, 790);
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = Back;
        ForeColor = Ink;
        Font = new Font("Microsoft YaHei UI", 10f);
        KeyPreview = true;
        BuildLayout();
        RefreshPorts();
        dryMode.CheckedChanged += (_, _) => SetMode();
        refreshButton.Click += (_, _) => RefreshPorts();
        connectButton.Click += async (_, _) =>
        {
            if (serial?.IsOpen == true) await DisconnectAsync("用户断开");
            else await ConnectAsync();
        };
        KeyDown += OnKeyDown;
        KeyUp += OnKeyUp;
        Deactivate += (_, _) => ClearInputAndStop("窗口失去焦点");
        FormClosing += OnFormClosing;
        SetMode();
    }

    private void BuildLayout()
    {
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 4, Padding = new Padding(18, 14, 18, 14) };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 62));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 64));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 164));
        Controls.Add(root);

        var header = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1 };
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        header.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        var titleStack = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false };
        titleStack.Controls.Add(new Label { Text = "RM 步兵战车", Font = new Font(Font.FontFamily, 19, FontStyle.Bold), ForeColor = Ink, AutoSize = true });
        titleStack.Controls.Add(new Label { Text = "键鼠控制台  ·  HC-05 蓝牙串口", Font = new Font(Font.FontFamily, 9, FontStyle.Regular), ForeColor = Muted, AutoSize = true });
        var emergency = MakeButton("急停", Red, Color.White, 120, 44);
        emergency.Click += (_, _) => ClearInputAndStop("手动急停");
        header.Controls.Add(titleStack, 0, 0);
        header.Controls.Add(emergency, 1, 0);
        root.Controls.Add(header, 0, 0);

        var connection = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, FlowDirection = FlowDirection.LeftToRight, Padding = new Padding(0, 12, 0, 0) };
        connection.Controls.Add(new Label { Text = "串口", AutoSize = true, ForeColor = Muted, Margin = new Padding(0, 8, 8, 0) });
        connection.Controls.Add(ports);
        connection.Controls.Add(refreshButton);
        connection.Controls.Add(connectButton);
        connection.Controls.Add(new Label { Text = "9600 · 8N1", AutoSize = true, ForeColor = Muted, Margin = new Padding(4, 9, 16, 0) });
        connection.Controls.Add(dryMode);
        connection.Controls.Add(connectionState);
        connectionState.Margin = new Padding(18, 8, 0, 0);
        root.Controls.Add(connection, 0, 1);

        var content = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 1, Padding = new Padding(0, 6, 0, 12) };
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 31));
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 38));
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 31));
        var drive = MakeSection("底盘", "W / S 前后    A / D 横移    Q / E 转向");
        var center = MakeSection("操控状态", "键盘按住即动，松开即停");
        var gimbal = MakeSection("云台", "右键按住并移动鼠标调整方向");
        content.Controls.Add(drive, 0, 0);
        content.Controls.Add(center, 1, 0);
        content.Controls.Add(gimbal, 2, 0);

        var drivePad = new DirectionPad { Dock = DockStyle.Fill, Margin = new Padding(20, 10, 20, 0), BackColor = Panel };
        drive.Controls.Add(drivePad, 0, 2);
        var speedRow = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, Padding = new Padding(14, 6, 0, 0) };
        speedRow.Controls.Add(new Label { Text = "速度", AutoSize = true, ForeColor = Muted, Margin = new Padding(0, 7, 10, 0) });
        var low = MakeButton("低速", Raised, Ink, 72, 32);
        var high = MakeButton("高速", Raised, Ink, 72, 32);
        low.Click += (_, _) => SetSpeed(false);
        high.Click += (_, _) => SetSpeed(true);
        speedRow.Controls.Add(low);
        speedRow.Controls.Add(high);
        speedRow.Controls.Add(speedState);
        speedState.Margin = new Padding(12, 7, 0, 0);
        drive.Controls.Add(speedRow, 0, 3);

        var status = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, Padding = new Padding(18, 18, 12, 12) };
        status.Controls.Add(new Label { Text = "当前输入", ForeColor = Muted, AutoSize = true });
        motionState.Font = new Font(Font.FontFamily, 20, FontStyle.Bold);
        motionState.Margin = new Padding(0, 4, 0, 18);
        status.Controls.Add(motionState);
        vectorState.ForeColor = Mint;
        vectorState.Margin = new Padding(0, 0, 0, 24);
        status.Controls.Add(vectorState);
        pongState.Margin = new Padding(0, 0, 0, 8);
        telemetryState.Margin = new Padding(0, 0, 0, 8);
        status.Controls.Add(pongState);
        status.Controls.Add(telemetryState);
        status.Controls.Add(new Label { Text = "发射功能已锁定", ForeColor = Amber, AutoSize = true, Margin = new Padding(0, 18, 0, 0) });
        center.Controls.Add(status, 0, 2);

        aimSurface.Dock = DockStyle.Fill;
        aimSurface.Margin = new Padding(12, 10, 12, 0);
        aimSurface.BackColor = Color.FromArgb(23, 32, 33);
        aimSurface.Cursor = Cursors.Cross;
        aimSurface.Paint += PaintAimSurface;
        aimSurface.MouseDown += AimMouseDown;
        aimSurface.MouseMove += AimMouseMove;
        aimSurface.MouseUp += AimMouseUp;
        aimSurface.MouseCaptureChanged += (_, _) => { if (aiming && !aimSurface.Capture) EndAim(); };
        gimbal.Controls.Add(aimSurface, 0, 2);
        var aimInfo = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, Padding = new Padding(14, 8, 6, 4) };
        mouseHint.ForeColor = Muted;
        aimInfo.Controls.Add(mouseHint);
        yawState.Margin = new Padding(0, 12, 0, 3);
        pitchState.Margin = new Padding(0, 0, 0, 3);
        aimInfo.Controls.Add(yawState);
        aimInfo.Controls.Add(pitchState);
        gimbal.Controls.Add(aimInfo, 0, 3);
        root.Controls.Add(content, 0, 2);

        var logSection = MakeSection("通信记录", "最近的固件响应");
        log.Dock = DockStyle.Fill;
        log.BackColor = Panel;
        log.ForeColor = Ink;
        log.Font = new Font("Consolas", 9.5f);
        log.Margin = new Padding(14, 0, 14, 10);
        logSection.Controls.Add(log, 0, 2);
        root.Controls.Add(logSection, 0, 3);

        StyleTree(this);
    }

    private TableLayoutPanel MakeSection(string title, string subtitle)
    {
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, BackColor = Panel, Margin = new Padding(5, 0, 5, 0), ColumnCount = 1, RowCount = 4, Padding = new Padding(12) };
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 88));
        var heading = new Label { Text = title, ForeColor = Ink, Font = new Font(Font.FontFamily, 12, FontStyle.Bold), Dock = DockStyle.Fill };
        var sub = new Label { Text = subtitle, ForeColor = Muted, Font = new Font(Font.FontFamily, 8.5f), Dock = DockStyle.Fill, AutoEllipsis = true };
        layout.Controls.Add(heading, 0, 0);
        layout.Controls.Add(sub, 0, 1);
        return layout;
    }

    private static Button MakeButton(string title, Color back, Color fore, int width, int height) => new()
    {
        Text = title,
        Width = width,
        Height = height,
        BackColor = back,
        ForeColor = fore,
        FlatStyle = FlatStyle.Flat,
        FlatAppearance = { BorderSize = 0 },
        Margin = new Padding(4, 0, 4, 0),
        Cursor = Cursors.Hand
    };

    private static void StyleTree(Control parent)
    {
        foreach (Control child in parent.Controls)
        {
            if (child is ComboBox combo)
            {
                combo.BackColor = Raised;
                combo.ForeColor = Ink;
                combo.FlatStyle = FlatStyle.Flat;
            }
            else if (child is CheckBox check)
            {
                check.ForeColor = Ink;
                check.Margin = new Padding(10, 8, 0, 0);
            }
            StyleTree(child);
        }
    }

    private void RefreshPorts()
    {
        var selected = ports.SelectedItem as string;
        ports.Items.Clear();
        ports.Items.AddRange(SerialPort.GetPortNames().OrderBy(p => p, StringComparer.OrdinalIgnoreCase).Cast<object>().ToArray());
        if (selected is not null && ports.Items.Contains(selected)) ports.SelectedItem = selected;
        else if (ports.Items.Count > 0) ports.SelectedIndex = 0;
        AddLog(ports.Items.Count == 0 ? "未发现串口。" : $"发现 {ports.Items.Count} 个串口。");
    }

    private void SetMode()
    {
        var offline = dryMode.Checked;
        ports.Enabled = refreshButton.Enabled = !offline;
        connectButton.Enabled = !offline || serial?.IsOpen == true;
        if (offline && serial?.IsOpen == true) _ = DisconnectAsync("切换到离线演练");
        connectionState.Text = offline ? "离线演练：不会写入串口" : serial?.IsOpen == true ? "串口已连接" : "未连接";
        connectionState.ForeColor = offline ? Amber : serial?.IsOpen == true ? Mint : Muted;
    }

    private async Task ConnectAsync()
    {
        if (ports.SelectedItem is not string port)
        {
            MessageBox.Show(this, "先选择一个 HC-05 传出串口。", "无法连接", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }
        try
        {
            var next = new SerialPort(port, 9600, Parity.None, 8, StopBits.One)
            {
                Handshake = Handshake.None,
                ReadTimeout = 100,
                WriteTimeout = 300,
                DtrEnable = true,
                RtsEnable = true
            };
            next.DataReceived += OnDataReceived;
            next.ErrorReceived += (_, e) => OnSerialFault("串口错误：" + e.EventType);
            next.Open();
            lock (serialGate)
            {
                serial = next;
                protocol = new RmProtocol();
                protocol.TextReceived += OnTextReceived;
                lastPong = lastPing = DateTime.UtcNow;
            }
            pumpStop = new CancellationTokenSource();
            pumpTask = Task.Run(() => PumpAsync(pumpStop.Token));
            connectButton.Text = "断开";
            connectButton.BackColor = Raised;
            connectionState.Text = $"已连接 {port}，等待 PONG";
            connectionState.ForeColor = Amber;
            AddLog($"连接 {port} @ 9600 8N1");
            SendFrame(RmProtocol.Ping);
            SendFrame(RmProtocol.Telem, [1]);
        }
        catch (Exception ex)
        {
            AddLog("连接失败：" + ex.Message);
            MessageBox.Show(this, ex.Message, "串口连接失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            await DisconnectAsync("连接失败");
        }
    }

    private async Task DisconnectAsync(string reason)
    {
        lock (inputGate) down.Clear();
        EndAim();
        lock (serialGate)
        {
            if (serial?.IsOpen == true)
            {
                try { serial.Write(protocol.Build(RmProtocol.Stop, []), 0, 6); }
                catch { }
                try { serial.Write(protocol.BuildVelocity(0, 0, 0), 0, 12); }
                catch { }
            }
            if (serial is not null)
            {
                serial.DataReceived -= OnDataReceived;
                try { if (serial.IsOpen) serial.Close(); } catch { }
                serial.Dispose();
                serial = null;
            }
        }
        pumpStop?.Cancel();
        try { if (pumpTask is not null) await pumpTask; } catch { }
        pumpStop?.Dispose();
        pumpStop = null;
        pumpTask = null;
        connectButton.Text = "连接";
        connectionState.Text = dryMode.Checked ? "离线演练：不会写入串口" : "未连接";
        connectionState.ForeColor = dryMode.Checked ? Amber : Muted;
        pongState.Text = "固件未连接";
        telemetryState.Text = "遥测等待连接";
        motionState.Text = "静止";
        vectorState.Text = "VX 0    VY 0    WZ 0";
        AddLog(reason);
    }

    private async Task PumpAsync(CancellationToken token)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(50));
        try
        {
            while (await timer.WaitForNextTickAsync(token))
            {
                var (vx, vy, wz) = ReadVelocity();
                if (serial?.IsOpen != true) return;
                SendFrame(RmProtocol.Vel, VelocityPayload(vx, vy, wz));
                var now = DateTime.UtcNow;
                if ((now - lastPing).TotalMilliseconds >= 500)
                {
                    SendFrame(RmProtocol.Ping);
                    lastPing = now;
                }
                if ((now - lastPong).TotalSeconds > 2.0)
                {
                    OnSerialFault("2 秒内没有收到 PONG，已停止控制。");
                    return;
                }
                UpdateMotionDisplay(vx, vy, wz);
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { OnSerialFault("控制发送失败：" + ex.Message); }
    }

    private static byte[] VelocityPayload(short vx, short vy, short wz)
    {
        var frame = new RmProtocol().BuildVelocity(vx, vy, wz);
        return frame[5..11];
    }

    private (short vx, short vy, short wz) ReadVelocity()
    {
        lock (inputGate)
        {
            var speed = down.Contains(Keys.ShiftKey) ? 600 : 300;
            var vx = (down.Contains(Keys.W) ? speed : 0) - (down.Contains(Keys.S) ? speed : 0);
            var vy = (down.Contains(Keys.A) ? speed : 0) - (down.Contains(Keys.D) ? speed : 0);
            var wz = (down.Contains(Keys.Q) ? speed : 0) - (down.Contains(Keys.E) ? speed : 0);
            return ((short)Math.Clamp(vx, -700, 700), (short)Math.Clamp(vy, -700, 700), (short)Math.Clamp(wz, -700, 700));
        }
    }

    private void UpdateMotionDisplay(short vx, short vy, short wz)
    {
        if (IsDisposed || !IsHandleCreated) return;
        BeginInvoke(() =>
        {
            var active = vx != 0 || vy != 0 || wz != 0;
            motionState.Text = active ? "行驶中" : "静止";
            motionState.ForeColor = active ? Mint : Ink;
            vectorState.Text = $"VX {vx}    VY {vy}    WZ {wz}";
            speedState.Text = (Math.Abs(vx) > 300 || Math.Abs(vy) > 300 || Math.Abs(wz) > 300) ? "高速  600" : "低速  300";
        });
    }

    private void SetSpeed(bool high)
    {
        lock (inputGate)
        {
            if (high) down.Add(Keys.ShiftKey);
            else down.Remove(Keys.ShiftKey);
        }
        var velocity = ReadVelocity();
        UpdateMotionDisplay(velocity.vx, velocity.vy, velocity.wz);
    }

    private void SendFrame(byte command, byte[]? payload = null)
    {
        lock (serialGate)
        {
            if (serial?.IsOpen != true) return;
            var bytes = protocol.Build(command, payload ?? []);
            serial.Write(bytes, 0, bytes.Length);
        }
    }

    private void SendServo()
    {
        yaw10 = Math.Clamp(yaw10, 0, 1800);
        pitch10 = Math.Clamp(pitch10, 200, 1600);
        yawState.Text = $"Yaw  {yaw10 / 10.0:0.0}°  (指令角)";
        pitchState.Text = $"Pitch  {pitch10 / 10.0:0.0}°  (指令角)";
        aimSurface.Invalidate();
        if (!dryMode.Checked && serial?.IsOpen == true)
        {
            try { SendFrame(RmProtocol.Servo, ServoPayload((short)yaw10, (short)pitch10)); }
            catch (Exception ex) { OnSerialFault("云台发送失败：" + ex.Message); }
        }
        else AddLog($"演练：云台目标 {yaw10 / 10.0:0.0}° / {pitch10 / 10.0:0.0}°");
    }

    private static byte[] ServoPayload(short yaw, short pitch)
    {
        var frame = new RmProtocol().BuildServo(yaw, pitch);
        return frame[5..9];
    }

    private void OnKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Escape)
        {
            ClearInputAndStop("Esc 急停");
            if (serial?.IsOpen == true) _ = DisconnectAsync("Esc 断开串口");
            e.Handled = true;
            e.SuppressKeyPress = true;
            return;
        }
        if (e.KeyCode == Keys.X)
        {
            ClearInputAndStop("X 急停");
            e.Handled = true;
            return;
        }
        if (e.KeyCode == Keys.R)
        {
            yaw10 = 900;
            pitch10 = 900;
            SendServo();
            e.Handled = true;
            return;
        }
        if (e.KeyCode is Keys.W or Keys.A or Keys.S or Keys.D or Keys.Q or Keys.E or Keys.ShiftKey)
        {
            lock (inputGate) down.Add(e.KeyCode);
            var velocity = ReadVelocity();
            UpdateMotionDisplay(velocity.vx, velocity.vy, velocity.wz);
            e.Handled = true;
            e.SuppressKeyPress = true;
        }
    }

    private void OnKeyUp(object? sender, KeyEventArgs e)
    {
        lock (inputGate) down.Remove(e.KeyCode);
        if (e.KeyCode is Keys.W or Keys.A or Keys.S or Keys.D or Keys.Q or Keys.E or Keys.ShiftKey)
        {
            var velocity = ReadVelocity();
            UpdateMotionDisplay(velocity.vx, velocity.vy, velocity.wz);
            e.Handled = true;
            e.SuppressKeyPress = true;
        }
    }

    private void ClearInputAndStop(string reason)
    {
        lock (inputGate) down.Clear();
        EndAim();
        if (dryMode.Checked)
        {
            UpdateMotionDisplay(0, 0, 0);
            AddLog(reason + "：离线演练，没有串口写入。");
            return;
        }
        try
        {
            SendFrame(RmProtocol.Stop);
            SendFrame(RmProtocol.Vel, [0, 0, 0, 0, 0, 0]);
            UpdateMotionDisplay(0, 0, 0);
            AddLog(reason + "：已发送 STOP 和零速度。");
        }
        catch (Exception ex) { OnSerialFault(reason + "，停车帧发送失败：" + ex.Message); }
    }

    private void AimMouseDown(object? sender, MouseEventArgs e)
    {
        if (e.Button != MouseButtons.Right) return;
        aiming = true;
        aimSurface.Capture = true;
        Cursor.Clip = aimSurface.RectangleToScreen(aimSurface.ClientRectangle);
        lastMouse = Cursor.Position;
        mouseHint.Text = "正在调整云台；松开右键停止输入";
    }

    private void AimMouseMove(object? sender, MouseEventArgs e)
    {
        if (!aiming || !Control.MouseButtons.HasFlag(MouseButtons.Right)) return;
        var current = Cursor.Position;
        var dx = current.X - lastMouse.X;
        var dy = current.Y - lastMouse.Y;
        if (dx == 0 && dy == 0) return;
        yaw10 = Math.Clamp(yaw10 + dx * 2, 0, 1800);
        pitch10 = Math.Clamp(pitch10 - dy * 2, 200, 1600);
        SendServo();
        var center = aimSurface.PointToScreen(new Point(aimSurface.Width / 2, aimSurface.Height / 2));
        lastMouse = center;
        Cursor.Position = center;
    }

    private void AimMouseUp(object? sender, MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Right) EndAim();
    }

    private void EndAim()
    {
        if (!aiming) return;
        aiming = false;
        aimSurface.Capture = false;
        Cursor.Clip = Rectangle.Empty;
        mouseHint.Text = "按住此区域右键并移动鼠标，调整云台";
    }

    private void PaintAimSurface(object? sender, PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        var w = aimSurface.ClientSize.Width;
        var h = aimSurface.ClientSize.Height;
        using var gridPen = new Pen(Color.FromArgb(48, 72, 72), 1);
        for (var x = 0; x < w; x += 28) g.DrawLine(gridPen, x, 0, x, h);
        for (var y = 0; y < h; y += 28) g.DrawLine(gridPen, 0, y, w, y);
        using var crossPen = new Pen(Mint, 2);
        var cx = w / 2;
        var cy = h / 2;
        g.DrawLine(crossPen, cx - 18, cy, cx + 18, cy);
        g.DrawLine(crossPen, cx, cy - 18, cx, cy + 18);
        using var ringPen = new Pen(Color.FromArgb(100, Mint), 1);
        g.DrawEllipse(ringPen, cx - 38, cy - 38, 76, 76);
    }

    private void OnDataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        try
        {
            lock (serialGate)
            {
                if (serial?.IsOpen != true) return;
                var count = serial.BytesToRead;
                if (count <= 0) return;
                var bytes = new byte[count];
                serial.Read(bytes, 0, count);
                foreach (var frame in protocol.Feed(bytes)) HandleFrame(frame);
            }
        }
        catch (Exception ex) { OnSerialFault("接收错误：" + ex.Message); }
    }

    private void HandleFrame(RmFrame frame)
    {
        if (frame.Command == RmProtocol.RspPong && frame.Payload.Length >= 7)
        {
            lastPong = DateTime.UtcNow;
            var version = $"{frame.Payload[0]}.{frame.Payload[1]}.{frame.Payload[2]}";
            var uptime = BitConverter.ToUInt32(frame.Payload, 3);
            BeginUi(() =>
            {
                pongState.Text = $"固件 v{version}  ·  运行 {uptime / 1000}s";
                connectionState.Text = $"串口正常  ·  {serial?.PortName}";
                connectionState.ForeColor = Mint;
            });
            return;
        }
        if (frame.Command == RmProtocol.RspTelem)
        {
            var t = RmTelemetry.Parse(frame.Payload);
            if (t is null) return;
            BeginUi(() =>
            {
                var lockState = (t.Flags & 0x04) != 0 ? "主控启动锁定" : "主控运行";
                telemetryState.Text = t.BatteryMv == 0
                    ? $"遥测：{lockState}  ·  电压未接入  ·  VX {t.Vx}, VY {t.Vy}, WZ {t.Wz}"
                    : $"遥测：{lockState}  ·  电池 {t.BatteryMv / 1000.0:0.00}V  ·  VX {t.Vx}, VY {t.Vy}, WZ {t.Wz}";
            });
            return;
        }
        AddLog($"收到帧 0x{frame.Command:X2}，{frame.Payload.Length} 字节");
    }

    private void OnTextReceived(string line) => AddLog("固件：" + line);

    private void OnSerialFault(string message)
    {
        if (closing || IsDisposed) return;
        BeginUi(() =>
        {
            AddLog(message);
            connectionState.Text = "通信异常，控制已停止";
            connectionState.ForeColor = Red;
            _ = DisconnectAsync(message);
        });
    }

    private void BeginUi(Action action)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try { BeginInvoke(action); } catch (InvalidOperationException) { }
    }

    private void AddLog(string message)
    {
        if (IsDisposed || !IsHandleCreated) return;
        BeginUi(() =>
        {
            log.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}");
            while (log.Lines.Length > 120)
            {
                var firstLine = log.GetFirstCharIndexFromLine(1);
                if (firstLine < 0) break;
                log.Select(0, firstLine);
                log.SelectedText = "";
            }
            log.SelectionStart = log.TextLength;
            log.ScrollToCaret();
        });
    }

    private void OnFormClosing(object? sender, FormClosingEventArgs e)
    {
        closing = true;
        if (serial?.IsOpen == true)
        {
            try
            {
                lock (serialGate)
                {
                    var stop = protocol.Build(RmProtocol.Stop, []);
                    serial.Write(stop, 0, stop.Length);
                    var zero = protocol.BuildVelocity(0, 0, 0);
                    serial.Write(zero, 0, zero.Length);
                }
            }
            catch { }
        }
        Cursor.Clip = Rectangle.Empty;
        try { serial?.Close(); } catch { }
        pumpStop?.Cancel();
    }

    private sealed class DirectionPad : Panel
    {
        public DirectionPad()
        {
            DoubleBuffered = true;
            MinimumSize = new Size(180, 160);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            var center = new Point(Width / 2, Height / 2);
            using var outline = new Pen(Color.FromArgb(70, 94, 91), 1);
            using var active = new SolidBrush(Color.FromArgb(55, Mint));
            using var idle = new SolidBrush(Raised);
            using var keyFont = new Font("Segoe UI", 12, FontStyle.Bold);
            DrawKey(e.Graphics, "W", center.X, center.Y - 53, idle, outline, keyFont);
            DrawKey(e.Graphics, "A", center.X - 53, center.Y, idle, outline, keyFont);
            DrawKey(e.Graphics, "S", center.X, center.Y, idle, outline, keyFont);
            DrawKey(e.Graphics, "D", center.X + 53, center.Y, idle, outline, keyFont);
            DrawKey(e.Graphics, "Q", center.X - 53, center.Y + 53, idle, outline, keyFont);
            DrawKey(e.Graphics, "E", center.X + 53, center.Y + 53, idle, outline, keyFont);
        }

        private static void DrawKey(Graphics g, string text, int cx, int cy, Brush fill, Pen outline, Font font)
        {
            var rect = new Rectangle(cx - 21, cy - 19, 42, 38);
            g.FillRectangle(fill, rect);
            g.DrawRectangle(outline, rect);
            TextRenderer.DrawText(g, text, font, rect, Color.White, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        }
    }
}
