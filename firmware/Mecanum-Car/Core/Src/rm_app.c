/**
  ******************************************************************************
  * @file    rm_app.c
  * @brief   应用层实现
  *
  * 结构（对照 4 周计划里的分层）：
  *   1ms   : 发射软件 PWM + 串口取数
  *   10ms  : 安全监管 -> 底盘解算 -> 电机斜坡 -> 舵机平滑 -> 发射状态机
  *   100ms : 电压采样 + 遥测上报
  *
  * 为什么不用 HAL_Delay 做周期：HAL_Delay 会卡死整个调度，
  *   一旦某个任务耗时变长，其它任务全乱。这里用"到点执行"的节拍器。
  *
  * 安全监管优先级（谁都能停车，最后一道闸）：
  *   上电锁定 1s -> 失联 500ms -> 欠压 -> 点动自动退出
  ******************************************************************************
  */

#include "rm_app.h"
#include "rm_config.h"
#include "rm_protocol.h"
#include "rm_remote.h"
#include "rm_motor.h"
#include "rm_chassis.h"
#include "rm_servo.h"
#include "rm_shooter.h"
#include "rm_monitor.h"
#if (RM_ENABLE_VISION != 0)
#include "rm_vision.h"
#endif
#if (RM_ENABLE_REFEREE != 0)
#include "rm_referee.h"
#endif
#if (RM_ENABLE_KBM != 0)
#include "rm_kbm.h"
#endif

/* ==========================================================================
 * 状态位（遥测上报）
 * ========================================================================== */
#define RM_FLAG_LINK     0x01u
#define RM_FLAG_LOWV     0x02u
#define RM_FLAG_LOCK     0x04u
#define RM_FLAG_FIRE     0x08u
#define RM_FLAG_VBAT     0x10u
#define RM_FLAG_KBM      0x20u

static uint8_t  s_telem_on;
static uint8_t  s_locked;
static uint32_t s_boot_ms;

static uint32_t s_t10;
static uint32_t s_t100;

#if (RM_ENABLE_KBM != 0)
/* 左键状态机：短按=单发、按住=连发；断线恢复后必须先松开一次才允许开火 */
static uint32_t s_btn_press_ms;
static uint8_t  s_fire_armed;

static void report_kbm(void);
#endif

/* ==========================================================================
 * 小工具：不用 printf，自己拼字符串（省 flash）
 * ========================================================================== */
static char *str_put(char *p, const char *s)
{
    while (*s != '\0')
    {
        *p = *s;
        p++;
        s++;
    }
    *p = '\0';
    return p;
}

static char *str_put_int(char *p, int32_t v)
{
    char tmp[12];
    uint8_t n = 0u;
    uint32_t u;

    if (v < 0)
    {
        *p = '-';
        p++;
        u = (uint32_t)(-v);
    }
    else
    {
        u = (uint32_t)v;
    }

    if (u == 0u)
    {
        *p = '0';
        p++;
        *p = '\0';
        return p;
    }

    while ((u > 0u) && (n < 11u))
    {
        tmp[n] = (char)('0' + (u % 10u));
        u = u / 10u;
        n++;
    }
    while (n > 0u)
    {
        n--;
        *p = tmp[n];
        p++;
    }
    *p = '\0';
    return p;
}

/* ==========================================================================
 * 标定报告：把当前方向/trim/限幅打印出来，抄回 rm_config.h 就固化了
 * ========================================================================== */
static void report_cal(void)
{
    char  buf[96];
    char *p = buf;
    uint8_t i;

    p = str_put(p, "cal sign ");
    for (i = 0u; i < RM_MOTOR_COUNT; i++)
    {
        p = str_put_int(p, (int32_t)Motor_GetSign(i));
        *p = ' ';
        p++;
    }
    p = str_put(p, "trim ");
    for (i = 0u; i < RM_MOTOR_COUNT; i++)
    {
        p = str_put_int(p, (int32_t)Motor_GetTrim(i));
        *p = ' ';
        p++;
    }
    p = str_put(p, "limit ");
    p = str_put_int(p, (int32_t)RM_MOTOR_LIMIT);
    *p = '\r'; p++;
    *p = '\n'; p++;
    *p = '\0';

    Remote_TxText(buf);
}

/* ==========================================================================
 * 裁判系统联动
 *
 * 默认不编译（RM_ENABLE_REFEREE=0）。开启后由 RM_App_RefereeRx() 喂原始字节，
 * 这里只做"把比赛数据变成保护动作"：
 *   - 热量：未发弹时 heat 已经贴到上限 -> 禁止连发（避免熔弹丸卡弹）
 *   - 血量：hp=0 时锁底盘（死车不能继续冲，规则里这是硬性要求）
 *   - 0x85 帧把裁判数据带上，上位机不用额外接线就能看到
 * ========================================================================== */
#if (RM_ENABLE_REFEREE != 0)
static uint8_t referee_heat_blocked(void)
{
    const rm_referee_status_t *r = Referee_Get();

    if ((r->valid_mask & RM_REFEREE_VALID_POWER) == 0u)
    {
        return 0u;   /* 没收到过热量数据就放行（不能因为没接线就不能打） */
    }
    if (r->heat_limit == 0u)
    {
        return 0u;
    }
    /* 热量上限用 17mm 枪管（42mm 规则类似，本车只有 17mm） */
    if (r->heat_17mm_1 >= (uint16_t)(r->heat_limit - RM_REFEREE_HEAT_MARGIN))
    {
        return 1u;
    }
    return 0u;
}

#if (RM_REFEREE_HP_GUARD != 0)
static uint8_t referee_hp_zero(void)
{
    const rm_referee_status_t *r = Referee_Get();

    if ((r->valid_mask & RM_REFEREE_VALID_ROBOT) == 0u)
    {
        return 0u;
    }
    return (r->hp == 0u) ? 1u : 0u;
}
#endif

static void report_referee(void)
{
    char  buf[96];
    char *p = buf;
    const rm_referee_status_t *r = Referee_Get();

    p = str_put(p, "ref ");
    p = str_put_int(p, (int32_t)r->robot_id);
    p = str_put(p, " hp ");
    p = str_put_int(p, (int32_t)r->hp);
    *p = '/'; p++;
    p = str_put_int(p, (int32_t)r->max_hp);
    p = str_put(p, " heat ");
    p = str_put_int(p, (int32_t)r->heat_17mm_1);
    *p = '/'; p++;
    p = str_put_int(p, (int32_t)r->heat_limit);
    p = str_put(p, " cool ");
    p = str_put_int(p, (int32_t)r->cooling_rate);
    p = str_put(p, " power ");
    p = str_put_int(p, (int32_t)r->chassis_power_limit);
    p = str_put(p, " online ");
    p = str_put_int(p, (int32_t)(Referee_IsOnline(Remote_GetLastRxMs()) ? 1 : 0));
    *p = '\r'; p++;
    *p = '\n'; p++;
    *p = '\0';

    Remote_TxText(buf);
}

static void report_rftest(void)
{
    char  buf[48];
    char *p = buf;

    p = str_put(p, "rftest ");
    p = str_put_int(p, (Referee_SelfTest(HAL_GetTick()) != 0u) ? 1 : 0);
    p = str_put(p, " frames ");
    p = str_put_int(p, (int32_t)Referee_Get()->frame_count);
    p = str_put(p, " crc ");
    p = str_put_int(p, (int32_t)Referee_Get()->crc_error_count);
    *p = '\r'; p++;
    *p = '\n'; p++;
    *p = '\0';

    Remote_TxText(buf);
}
#endif /* RM_ENABLE_REFEREE */

/* ==========================================================================
 * 指令分发（帧指令和文本指令都走这里）
 * ========================================================================== */
static void rm_cmd_handler(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    Remote_LinkTimerReset();

    switch (cmd)
    {
        case RM_CMD_VEL:
            if (len >= 6u)
            {
                if (s_locked != 0u)
                {
                    Chassis_Stop();
                }
                else
                {
                    Chassis_SetVelocity(Proto_GetI16(&payload[0]),
                                        Proto_GetI16(&payload[2]),
                                        Proto_GetI16(&payload[4]));
                }
            }
            break;

        case RM_CMD_SERVO:
            /* 4 字节 = Yaw + Pitch（x10）；2 字节 = 只动 Pitch，兼容老上位机 */
            if (len >= 4u)
            {
                Gimbal_SetDeg10(Proto_GetI16(&payload[0]), Proto_GetI16(&payload[2]));
            }
            else if (len >= 2u)
            {
                Servo_SetTargetDeg10(Proto_GetI16(&payload[0]));
            }
            break;

        case RM_CMD_FIRE:
            if (len >= 1u)
            {
                if (s_locked != 0u)
                {
                    Shooter_Stop();
                }
                else
                {
#if (RM_ENABLE_REFEREE != 0)
                    /* 发弹前先看热量：贴上限还连发会把弹丸烘热变形，反而卡弹 */
                    if ((payload[0] != 0u) && (referee_heat_blocked() != 0u))
                    {
                        Shooter_Stop();
                        Remote_TxText("heat limit\r\n");
                    }
                    else
                    {
                        Shooter_SetMode(payload[0]);
                    }
#else
                    Shooter_SetMode(payload[0]);
#endif
                }
            }
            break;

        case RM_CMD_WHEELTEST:
            if (len >= 3u)
            {
                if (s_locked == 0u)
                {
                    Chassis_SetTest(payload[0], Proto_GetI16(&payload[1]));
                }
            }
            break;

        case RM_CMD_FLIP:
            if (len >= 1u)
            {
                Motor_FlipSign(payload[0]);
                report_cal();
            }
            break;

        case RM_CMD_TELEM:
            if (len >= 1u)
            {
                s_telem_on = (payload[0] != 0u) ? 1u : 0u;
            }
            break;

        case RM_CMD_CAL:
            report_cal();
            break;

        case RM_CMD_REF:
#if (RM_ENABLE_REFEREE != 0)
            if ((len >= 1u) && (payload[0] == (uint8_t)'t'))
            {
                report_rftest();
            }
            else
            {
                report_referee();
            }
#endif
            break;

        case RM_CMD_KBM:
#if (RM_ENABLE_KBM != 0)
            report_kbm();
#endif
            break;

        case RM_CMD_PING:
            {
                uint8_t pong[7];
                pong[0] = (uint8_t)RM_VERSION_MAJOR_;
                pong[1] = (uint8_t)RM_VERSION_MINOR_;
                pong[2] = (uint8_t)RM_VERSION_PATCH_;
                Proto_PutU32(&pong[3], HAL_GetTick());
                Remote_TxFrame(RM_RSP_PONG, pong, 7u);
            }
            break;

        case RM_CMD_STOP:
            Chassis_Stop();
            Shooter_Stop();
            break;

        default:
            break;
    }
}

/* ==========================================================================
 * 图传键鼠控制（人工瞄准）
 *
 * 和蓝牙指令的区别：蓝牙是"一次性命令"，键鼠是"持续状态流"（10Hz 一帧），
 * 所以每 10ms 都用最新一帧重算一次底盘速度；一帧没来就按超时处理。
 *
 * 安全点：
 *   1) 键鼠失联 300ms -> 底盘/发射全停（视频卡了但键鼠没断不能靠这个，靠人按急停）
 *   2) 断线恢复后必须先松开左键才允许开火，绝不"续着上次的按压自动打"
 * ========================================================================== */
#if (RM_ENABLE_KBM != 0)
static void kbm_control(uint32_t now)
{
    const kbm_state_t *kb = Kbm_Get();
    int16_t speed;
    int16_t rot;
    int16_t vx = 0;
    int16_t vy = 0;
    int16_t wz = 0;

    if (Kbm_Online(now) == 0u)
    {
        s_btn_press_ms = 0u;
        s_fire_armed = 0u;
        Chassis_Stop();
        Shooter_Stop();
        return;
    }

    /* 松开过左键才重新允许开火 */
    if (kb->btn_left == 0u)
    {
        s_fire_armed = 1u;
    }

    /* 急停键最高优先级 */
    if ((kb->keys & KBM_KEY_STOP) != 0u)
    {
        Chassis_Stop();
        Shooter_Stop();
        s_btn_press_ms = 0u;
        return;
    }

    speed = ((kb->keys & KBM_KEY_SNIPE) != 0u) ? RM_KBM_SNIPE_SPEED : RM_KBM_SPEED;
    rot   = ((kb->keys & KBM_KEY_SNIPE) != 0u) ? RM_KBM_SNIPE_ROT : RM_KBM_ROT;

    if ((kb->keys & KBM_KEY_W) != 0u) { vx += speed; }
    if ((kb->keys & KBM_KEY_S) != 0u) { vx -= speed; }
    if ((kb->keys & KBM_KEY_A) != 0u) { vy += speed; }
    if ((kb->keys & KBM_KEY_D) != 0u) { vy -= speed; }
    if ((kb->keys & KBM_KEY_Q) != 0u) { wz += rot; }
    if ((kb->keys & KBM_KEY_E) != 0u) { wz -= rot; }

    Chassis_SetVelocity(vx, vy, wz);

    /* 鼠标上下 -> 俯仰目标角（向上为负 dy，角度减小=抬头） */
    if (kb->mouse_dy != 0)
    {
        int32_t target = (int32_t)Servo_GetTargetDeg10() -
                         ((int32_t)kb->mouse_dy * RM_KBM_PITCH_STEP_DEG10);
        int32_t lo = (int32_t)RM_SERVO_MIN_DEG * 10;
        int32_t hi = (int32_t)RM_SERVO_MAX_DEG * 10;

        if (target < lo) { target = lo; }
        if (target > hi) { target = hi; }
        Servo_SetTargetDeg10((int16_t)target);
    }

    /* 左键：短按三连发，按住超过阈值转连发，松开停 */
    if (kb->btn_left != 0u)
    {
        if (s_btn_press_ms == 0u)
        {
            s_btn_press_ms = (now != 0u) ? now : 1u;
            if (s_fire_armed != 0u)
            {
                Shooter_SetMode(3u);
            }
        }
        else if (((now - s_btn_press_ms) >= RM_KBM_FIRE_HOLD_MS) &&
                 (s_fire_armed != 0u) &&
                 (Shooter_GetMode() != 2u))
        {
            Shooter_SetMode(2u);
        }
    }
    else if (s_btn_press_ms != 0u)
    {
        s_btn_press_ms = 0u;
        Shooter_Stop();
    }
}

static void report_kbm(void)
{
    char  buf[80];
    char *p = buf;
    const kbm_state_t *kb = Kbm_Get();

    p = str_put(p, "kb keys ");
    p = str_put_int(p, (int32_t)kb->keys);
    p = str_put(p, " dx ");
    p = str_put_int(p, (int32_t)kb->mouse_dx);
    p = str_put(p, " dy ");
    p = str_put_int(p, (int32_t)kb->mouse_dy);
    p = str_put(p, " btn ");
    p = str_put_int(p, (int32_t)kb->btn_left);
    p = str_put(p, " frm ");
    p = str_put_int(p, (int32_t)kb->frame_count);
    p = str_put(p, " err ");
    p = str_put_int(p, (int32_t)kb->error_count);
    *p = '\r'; p++;
    *p = '\n'; p++;
    *p = '\0';

    Remote_TxText(buf);
}
#endif /* RM_ENABLE_KBM */

/* ==========================================================================
 * 安全监管
 * ========================================================================== */
static void safety_task(uint32_t now)
{
    const rm_status_t *st;

    /* 上电锁定：一上电 1s 内不接受任何运动指令，防止上电瞬间冲出去 */
    if ((now - s_boot_ms) < RM_STARTUP_LOCK_MS)
    {
        s_locked = 1u;
    }
    else
    {
        s_locked = 0u;
    }

#if (RM_ENABLE_REFEREE != 0)
#if (RM_REFEREE_HP_GUARD != 0)
    /* 死车不能动：血量归零后锁死底盘（规则要求，也是安全底线） */
    if (referee_hp_zero() != 0u)
    {
        s_locked = 1u;
    }
#endif
#endif

    if (s_locked != 0u)
    {
        Chassis_Stop();
        Shooter_Stop();
    }

    /* 失联保护：超过 500ms 没收到任何指令 -> 停车 */
    if (Remote_GetKeepAlive() != 0u)
    {
        if ((now - Remote_GetLastRxMs()) > RM_LINK_TIMEOUT_MS)
        {
            Chassis_Stop();
            Shooter_Stop();
        }
    }

    /* 点动标定：300ms 没有新指令就自动退出，避免手一抖车轮一直转 */
    if ((Chassis_IsTestMode() != 0u) &&
        ((now - Remote_GetLastRxMs()) > 300u))
    {
        Chassis_ClearTest();
    }

    /* 欠压保护：先禁发射，再禁底盘 */
    st = Monitor_Get();
    if (st->vbat_valid != 0u)
    {
        if ((uint32_t)st->vbat_mv <= (uint32_t)RM_VBAT_STOP_MV)
        {
            Shooter_Stop();
            Chassis_Stop();
        }
        else if ((uint32_t)st->vbat_mv <= (uint32_t)RM_VBAT_WARN_MV)
        {
            Shooter_Stop();
        }
    }
}

/* ==========================================================================
 * 遥测
 * ========================================================================== */
static uint8_t telemetry_flags(uint32_t now)
{
    const rm_status_t *st = Monitor_Get();
    uint8_t f = 0u;

    if ((now - Remote_GetLastRxMs()) <= RM_LINK_TIMEOUT_MS)
    {
        f |= RM_FLAG_LINK;
    }
    if (s_locked != 0u)
    {
        f |= RM_FLAG_LOCK;
    }
    if (Shooter_GetState() != (uint8_t)RM_FIRE_IDLE)
    {
        f |= RM_FLAG_FIRE;
    }
    if (st->vbat_valid != 0u)
    {
        f |= RM_FLAG_VBAT;
        if (st->low_voltage != 0u)
        {
            f |= RM_FLAG_LOWV;
        }
    }
#if (RM_ENABLE_KBM != 0)
    if (Kbm_Online(now) != 0u)
    {
        f |= RM_FLAG_KBM;
    }
#endif
    return f;
}

/* v3.4：视觉状态字节，塞进遥测第 18 字节（不占已有的 flags 位） */
static uint8_t vision_status_byte(void)
{
#if (RM_ENABLE_VISION != 0)
    uint8_t b = (uint8_t)(Vision_GetMode() & 0x03u);

    if (Vision_GetAim() != 0u)
    {
        b |= 0x04u;
    }
    if (Vision_IsValid() != 0u)
    {
        b |= 0x08u;
    }
    return b;
#else
    return 0u;
#endif
}

static void send_telemetry(uint32_t now)
{
    uint8_t  p[20];
    int16_t  vx;
    int16_t  vy;
    int16_t  wz;

    if (s_telem_on == 0u)
    {
        return;
    }

    Chassis_GetVelocity(&vx, &vy, &wz);

    p[0]  = (uint8_t)(Monitor_Get()->vbat_mv & 0xFFu);
    p[1]  = (uint8_t)((Monitor_Get()->vbat_mv >> 8) & 0xFFu);
    Proto_PutI16(&p[2], vx);
    Proto_PutI16(&p[4], vy);
    Proto_PutI16(&p[6], wz);
    Proto_PutI16(&p[8], Servo_GetDeg10());
    p[10] = Shooter_GetState();
    p[11] = telemetry_flags(now);
    Proto_PutU32(&p[12], now);
    Proto_PutI16(&p[16], Yaw_GetDeg10());       /* v3.1 追加：Yaw 当前角 */
    /* v3.4 追加：视觉状态（低 2 位模式，bit2 自动瞄准，bit3 有目标） */
    p[18] = vision_status_byte();
    p[19] = 0u;

    Remote_TxFrame(RM_RSP_TELEM, p, 20u);
}

/* ==========================================================================
 * 节拍任务
 * ========================================================================== */
static void task_10ms(uint32_t now)
{
    safety_task(now);

#if (RM_ENABLE_KBM != 0)
    kbm_control(now);
#endif

#if (RM_ENABLE_VBAT != 0)
    feed_battery();
#endif

    Chassis_SetGimbalYawDeg10(Yaw_GetDeg10());   /* 云台系驱动用（follow 1 才生效） */
    Chassis_Task();
    Motor_Task();
    Servo_Task();
#if (RM_ENABLE_VISION != 0)
    Vision_Task();                               /* 自动瞄准：只改目标角，不越权 */
#endif
    Shooter_Task();
}

#if (RM_ENABLE_VBAT != 0)
/* 把电压喂给底盘做补偿（开了 RM_VBAT_COMPENSATE 才真正生效） */
static void feed_battery(void)
{
    const rm_status_t *st = Monitor_Get();

    if (st->vbat_valid != 0u)
    {
        uint32_t mv = (uint32_t)st->vbat_mv;

        if (mv > 20000u)
        {
            mv = 20000u;
        }
        Chassis_SetBatteryMv((int16_t)mv);
    }
}
#endif

#if (RM_ENABLE_REFEREE != 0)
/* 0x85：裁判数据专帧，跟 0x81 分开，上位机解析互不影响 */
static void send_referee_telemetry(uint32_t now)
{
    uint8_t p[18];
    const rm_referee_status_t *r = Referee_Get();

    if ((s_telem_on == 0u) || (Referee_IsOnline(now) == 0u))
    {
        return;
    }

    Proto_PutU16(&p[0], (uint16_t)(r->valid_mask & 0x3Fu));
    Proto_PutU16(&p[2], r->last_cmd);
    Proto_PutU16(&p[4], (uint16_t)r->frame_count);
    Proto_PutU16(&p[6], (uint16_t)r->crc_error_count);
    Proto_PutU16(&p[8], r->heat_17mm_1);
    Proto_PutU16(&p[10], r->heat_limit);
    Proto_PutU16(&p[12], r->hp);
    Proto_PutU16(&p[14], r->max_hp);
    p[16] = (uint8_t)(r->chassis_power_limit & 0xFFu);
    p[17] = (uint8_t)(referee_heat_blocked() | (referee_hp_zero() << 1));

    Remote_TxFrame(RM_RSP_REF, p, 18u);
}
#endif

static void task_100ms(uint32_t now)
{
    Monitor_Task();
    send_telemetry(now);
#if (RM_ENABLE_REFEREE != 0)
    send_referee_telemetry(now);
#endif
}

/* ==========================================================================
 * 初始化
 * ========================================================================== */
#if (RM_ENABLE_IWDG != 0)
static void iwdg_init(void)
{
    IWDG->KR  = 0x5555u;            /* 允许写 PR/RLR */
    IWDG->PR  = (uint32_t)RM_IWDG_PRESCALER;
    IWDG->RLR = (uint32_t)RM_IWDG_RELOAD;
    IWDG->KR  = 0xAAAAu;            /* 先喂一次 */
    IWDG->KR  = 0xCCCCu;            /* 启动 */
}
#endif

void RM_App_Init(void)
{
    s_boot_ms  = HAL_GetTick();
    s_telem_on = RM_TELEM_DEFAULT_ON;
    s_locked   = 1u;
    s_t10      = s_boot_ms + 10u;
    s_t100     = s_boot_ms + 100u;

    /* 先让执行器进入安全状态，再开通信 */
    Motor_Init();
    Chassis_Init();
    Servo_Init();
    Shooter_Init();
    Monitor_Init();
#if (RM_ENABLE_VISION != 0)
    Vision_Init();
#endif

    Remote_Init();
    Remote_SetHandler(rm_cmd_handler);

#if (RM_ENABLE_REFEREE != 0)
    Referee_Init();
#endif

#if (RM_ENABLE_KBM != 0)
    s_btn_press_ms = 0u;
    s_fire_armed = 0u;
    Kbm_Init();
#endif

#if (RM_ENABLE_IWDG != 0)
    iwdg_init();
#endif
}

/* ==========================================================================
 * 主循环（1ms 节拍推进）
 * ========================================================================== */
void RM_App_Loop(void)
{
    uint32_t now = HAL_GetTick();

    /* 1ms 任务：发射软件 PWM + 把串口缓冲取空 */
    Shooter_PwmTick();
    Remote_Task();
#if (RM_ENABLE_KBM != 0)
    Kbm_Task(now);
#endif

#if (RM_ENABLE_IWDG != 0)
    IWDG->KR = 0xAAAAu;
#endif

    /* 到点执行，落后太多就重新对齐，避免任务堆叠 */
    if ((int32_t)(now - s_t10) >= 0)
    {
        task_10ms(now);
        s_t10 += 10u;
        if ((int32_t)(now - s_t10) > 50)
        {
            s_t10 = now + 10u;
        }
    }

    if ((int32_t)(now - s_t100) >= 0)
    {
        task_100ms(now);
        s_t100 += 100u;
        if ((int32_t)(now - s_t100) > 200)
        {
            s_t100 = now + 100u;
        }
    }
}

#if (RM_ENABLE_REFEREE != 0)
void RM_App_RefereeRx(const uint8_t *data, uint16_t len)
{
    if ((data != 0) && (len > 0u))
    {
        Referee_Feed(data, len, HAL_GetTick());
    }
}
#endif

