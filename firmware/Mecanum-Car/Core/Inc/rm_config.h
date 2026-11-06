/**
  ******************************************************************************
  * @file    rm_config.h
  * @brief   整车参数配置（v2.0，按你的实车硬件重写）
  *
  *   固件板级基线：STM32F103C8T6 + 两颗 TB6612；须与实物 PCB 逐点核对
  *   底盘：60mm 麦轮 ×4，PA0~PA3 PWM，当前开环
  *   电机：MC310 霍尔编码器版；详细电气/编码参数未由采购表确认
  *   云台：USART3 PB10 总线发送；协议/电压须与到货型号核对
  *   发射：PB14 外置 MOS 软件 PWM；波箱与时序尚未验收
  *   新 `降压PCB1_2026-11-03` Gerber 不属于已验证的本固件目标板
  *
  *   所有可调参数集中在这里，改参数只改这个文件。
  ******************************************************************************
  * 分层约定（调用方向只能从上往下）：
  *   rm_app.c      应用层：调度 / 安全监管 / 指令分发 / 遥控映射
  *   rm_chassis.c  解算层：麦轮逆运动学 + 加速度限制 + 电池补偿
  *   rm_motor.c    驱动层：4 路 PWM + 方向 + 斜坡 + 限幅 + 单轮标定
  *   rm_servo.c    执行器：俯仰舵机平滑 + 限位 + 上电慢回中
  *   rm_shooter.c  执行器：发射状态机（软启动 / 单发 / 连发 / 反转）
  *   rm_monitor.c  感知：电池电压（ADC 寄存器直读）
  *   rm_remote.c   通信：串口收发 + 文本指令 + 帧指令
  *   rm_kbm.c      通信：官方图传键鼠输入（USART2）
  *   rm_protocol.c 纯逻辑：帧编解码，不碰硬件
  ******************************************************************************
  */

#ifndef __RM_CONFIG_H
#define __RM_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* RM_HOST_TEST：主机自检（tests\chassis_follow_test.c）时绕开 HAL 头 */
#ifndef RM_HOST_TEST
#include "main.h"
#include "tim.h"
#include "usart.h"
#endif

/* ==========================================================================
 * 0. 版本
 * ========================================================================== */
#define RM_VERSION_MAJOR_         3
#define RM_VERSION_MINOR_         8
#define RM_VERSION_PATCH_         0
#define RM_VERSION_STR            "RM-INFANTRY-CAR 3.8"

/* ==========================================================================
 * 1. 硬件清单与物理参数（换零件时先改这里）
 * ========================================================================== */

/* --- MC310 霍尔编码器减速电机 ×4 --------------------------------------
 * 采购表没有完整型号/规格书，只能确认“MC310、带霍尔编码器”。电压、电流、
 * 减速比、编码器每圈计数与输出电平均未知；0 表示待实测/查手册，不能用于控制。
 * 当前四路固件没有采样编码器，底盘仍为开环占空比控制。 */
#define RM_310_SUPPLY_MV          0       /* 到货核对铭牌/规格书 */
#define RM_310_NOLOAD_RPM         0       /* 到货核对；不作为速度控制值 */
#define RM_310_RATED_MA           0       /* 到货核对 */
#define RM_310_STALL_MA           0       /* 到货核对；不得用手堵转测量 */
#define RM_310_GEAR_RATIO         0       /* 到货核对 */
#define RM_310_ENCODER_LINES      0       /* 到货核对 */
#define RM_310_CPR                0       /* 参数确定后按编码器计数模式计算 */

/* --- 旧控制板 TB6612FNG 参考边界 -----------------------------------------
 * 每通道连续 1.2A，四轮同时转向/堵转时很容易超。
 * 65% 限幅仅降低平均驱动，不是电流限制；脉冲峰值和堵转仍可能超额。
 * 没有电流反馈，禁止堵轮试验；驱动选型仍须核对实物电机堵转电流。 */
#define RM_TB6612_CONT_MA         1200
#define RM_TB6612_PEAK_MA         3200

/* 总线舵机额定值未在价格表中给出。先核对实际型号和厂家手册，
 * 不将旧 PWM 舵机的供电/脉宽/电流规格套用于当前两轴云台。 */
#define RM_SERVO_SUPPLY_V         0.0f    /* 两轴额定电压未从采购表确认 */
#define RM_SERVO_STALL_MA         0       /* 单轴/双轴峰值电流待查规格书和带载测量 */

/* --- 60mm 麦克纳姆轮 ----------------------------------------------------- */
#define RM_WHEEL_DIAMETER_MM      60
#define RM_WHEEL_BASE_MM          160     /* 左右轮距（按你的车实测改） */
#define RM_TRACK_MM               160     /* 前后轴距（按你的车实测改） */

/* ==========================================================================
 * 2. 定时器 / 引脚分配（见 docs/接线总表.md）
 * ========================================================================== */

/* 电机 PWM：按你打的这块 PCB（麦轮步兵小车2.0）的实际走线：
 *   4 路 PWM 全在 PA0~PA3（TIM2 CH1~CH4），1kHz（PSC=71, ARR=999），比较值 0~1000。
 *   -> 因此 PA2/PA3 不能再用作 USART2，图传键鼠链路在这块板上默认关闭（见第 8 节）。
 *   如果你的板子和图纸不一致，改这一行：
 *   1 = TIM4 PB6/PB7/PB8/PB9   （给 USART2 键鼠腾脚的改法，需要飞线）
 *   0 = TIM2 PA0~PA3           （本 PCB 默认） */
#define RM_MOTOR_USE_TIM4         0

/* 方向脚（不变）：FL=PA4/PA5  RL=PA6/PA7  RR=PB10/PB11  FR=PB12/PB13 */
#define RM_DIR_FL_PORT            GPIOA
#define RM_DIR_FL_IN1             GPIO_PIN_4
#define RM_DIR_FL_IN2             GPIO_PIN_5
#define RM_DIR_RL_PORT            GPIOA
#define RM_DIR_RL_IN1             GPIO_PIN_6
#define RM_DIR_RL_IN2             GPIO_PIN_7
#define RM_DIR_FR_PORT            GPIOB
#define RM_DIR_FR_IN1             GPIO_PIN_12
#define RM_DIR_FR_IN2             GPIO_PIN_13

/* 舵机（TIM3_CH3，50Hz，比较值 = 脉宽 us） */
#define RM_SERVO_HTIM             (&htim3)
#define RM_SERVO_CHANNEL          TIM_CHANNEL_3
#define RM_SERVO_PULSE_MIN_US     500
#define RM_SERVO_PULSE_MAX_US     2500

/* ==========================================================================
 * 2b. 二维云台（Yaw + Pitch）
 *   两套件只换一个宏：
 *   1 = PWM 双舵机（推荐，板子只差一根信号线）
 *        Yaw   : PA8  TIM1_CH1 50Hz  <- 飞线到云台 Yaw 舵机信号
 *        Pitch : PB0  TIM3_CH3 50Hz  <- 板载舵机座
 *   2 = 总线舵机（套件"2.简易二维云台(总线)"）
 *        USART3 TX=PB10 115200，帧 #idPpwmTtime!（和套件例程同一套指令）
 *        代价：隔离 PB10 到 RR IN1 的原走线，IN1 飞到 PA15；IN2 仍为 PB11
 * ========================================================================== */
#define RM_GIMBAL_MODE            2
#define RM_GIMBAL_MODE_PWM        1
#define RM_GIMBAL_MODE_BUS        2

#define RM_GIMBAL_PULSE_MIN_US    500
#define RM_GIMBAL_PULSE_MAX_US    2500
/* 舵机全行程角度：普通 20KG/180° 舵机=180；松甲套件 270° 舵机（信号线白色）=270，
   改了之后把上面的 MIN/MAX_DEG 也按 270 重新标（例如 Yaw 0~270）。 */
#define RM_SERVO_SPAN_DEG         180
#define RM_GIMBAL_SLEW_10MS       30      /* 正常 3°/10ms = 300°/s */
#define RM_GIMBAL_HOME_SLEW_10MS  10      /* 上电回中 1°/10ms */
#define RM_GIMBAL_HOME_MS         1500

/* Yaw 轴 */
#define RM_GIMBAL_YAW_MIN_DEG     0
#define RM_GIMBAL_YAW_MAX_DEG     180
#define RM_GIMBAL_YAW_CENTER_DEG  90
#define RM_GIMBAL_YAW_INVERT      1       /* 左右反了改成 0 */

/* Pitch 轴限位/回中沿用上面的 RM_SERVO_MIN/MAX/CENTER_DEG（见第 3 节） */

/* 总线协议仅对应所附松甲例程；磁编码不代表兼容，须核对实物手册。
 * 开漏 PB10 需要合适的总线上拉/接口板，不能上拉到电池电压。 */
#define RM_GIMBAL_BUS_BAUD        115200
#define RM_GIMBAL_BUS_YAW_ID      1
#define RM_GIMBAL_BUS_PITCH_ID    2
#define RM_GIMBAL_BUS_TIME_MS     200     /* 舵机内部插值时间 */

/* 视觉模块（比赛提供的那个）：只收不发，要占 USART3 RX = PB11。
   但 PB11 是 RR 轮方向脚，本板又没有 PA15/PB1 的焊盘可以顶替 ——
   所以「四轮全向驱动 + 视觉」在本板上不能并存。默认关。
   要自瞄必须重新规划接口与驱动，本版本直接禁止开启。 */
#define RM_ENABLE_VISION          0

#if (RM_ENABLE_VISION != 0)
#error "视觉模块要吃 PB11，而 PB11 是 RR 方向脚；本板无替代引脚。详见 docs/视觉模块对接.md"
#endif

/* --------------------------------------------------------------------------
 * RR（右后轮）方向脚：总线云台占用 USART3 TX = PB10 之后只剩一条路
 *   IN1 = PA15  <- 板上没有焊盘，必须从最小系统板 PA15 飞一根线到 TB6612 输入脚
 *   IN2 = PB11  <- 原样不动（USART3 只开 TX，PB11 还是普通 GPIO）
 * 还要在 PCB 上把 PB10 到 TB6612 的那根走线割断，否则舵机串口数据会去拧电机方向。
 * -------------------------------------------------------------------------- */
#if (RM_GIMBAL_MODE == RM_GIMBAL_MODE_BUS)
#define RM_DIR_RR_PORT            GPIOA
#define RM_DIR_RR_IN1             GPIO_PIN_15
#define RM_DIR_RR_PORT2           GPIOB
#define RM_DIR_RR_IN2             GPIO_PIN_11
#define RM_RR_DIR_ALT             1
#else
#define RM_DIR_RR_PORT            GPIOB
#define RM_DIR_RR_IN1             GPIO_PIN_10
#define RM_DIR_RR_PORT2           RM_DIR_RR_PORT
#define RM_DIR_RR_IN2             GPIO_PIN_11
#define RM_RR_DIR_ALT             0
#endif

/* 发射 MOS（PB14）；若驱动板有两个输入，把 IN2 打开就能反转清卡弹 */
#define RM_FIRE_PORT              GPIOB
#define RM_FIRE_PIN               GPIO_PIN_14
#define RM_FIRE_IN2_ENABLE        0
#define RM_FIRE_IN2_PORT          GPIOB
#define RM_FIRE_IN2_PIN           GPIO_PIN_15

/* ==========================================================================
 * 3. 底盘 / 电机参数
 * 轮序固定：0=左前(FL) 1=左后(RL) 2=右后(RR) 3=右前(FR)
 * ========================================================================== */
#define RM_MOTOR_COUNT            4
#define RM_PWM_MAX                1000    /* ARR=999，比较值上限 1000 */

/* 总占空比限幅不等于限流，堵转仍会损坏驱动。 */
#define RM_MOTOR_LIMIT            650     /* 65% 占空比，不保证峰值电流安全 */
#define RM_MOTOR_DEADZONE         25      /* 小于 2.5% 占空比电机不转只发热，直接断电 */
#define RM_MOTOR_RAMP_STEP        35      /* 每 10ms 变化量 -> 3500/s；起步/换向不拉爆电流 */
#define RM_MOTOR_SOFT_RAMP_STEP   15      /* 上电前 300ms 用更慢的斜坡起步 */

/* 单轮方向标定：+1 / -1。架空用 m 指令逐轮点动，反了就 flip，最后 cal 抄回来。 */
#define RM_MOTOR_FWD_SIGN         {  1,  1, -1, -1 }

/* 单轮微调（跑偏用）：-200 ~ +200，往"走得慢的那一侧"加正数 */
#define RM_MOTOR_TRIM             {  0,  0,  0,  0 }
#define RM_MOTOR_TRIM_MIN         (-200)
#define RM_MOTOR_TRIM_MAX         200

/* 底盘手感 */
#define RM_LEGACY_SPEED           400     /* 单字符指令（WASD）的默认速度 */
#define RM_CHASSIS_ACCEL_STEP     60      /* 每 10ms 允许的指令变化量，防"点一下冲一下" */
#define RM_CHASSIS_MAX_SPEED      700     /* 任何指令的最终上限 */
#define RM_CHASSIS_INVERT_XY      0       /* 1 = 前后/左右整体反过来（装反时偷懒用） */

/* 摇杆死区：|指令| 小于它的直接当 0（手机蓝牙摇杆回中不漂）。
 * 键盘/帧指令是整值，不受影响；不想要就设 0。 */
#define RM_CHASSIS_DEADZONE       15

/* 云台系驱动（v3.2）：1 = "前进"沿炮口方向，0 = 沿车头。
 * 瞄准时横移不会把炮口带偏，是这个开关的唯一用途。运行中可用 follow 0/1 改。 */
#define RM_CHASSIS_FOLLOW_DEFAULT 0
#define RM_CHASSIS_FOLLOW_SIGN    1       /* 云台开着往前却往后退，就改成 -1 */

/* 电池补偿：相对 2S 满电电压按比例调整指令，保持手感一致。
 * 只在 RM_ENABLE_VBAT=1 且电压有效时生效，默认关闭。 */
#define RM_VBAT_FULL_MV           8400    /* 2S 满电（MC310 额定 7.4V，别用 3S） */
#define RM_VBAT_COMPENSATE        0       /* 1=开启电压补偿 */

/* ==========================================================================
 * 4. 舵机软件默认值（总线云台实物行程/映射未标定）
 * ========================================================================== */
#define RM_SERVO_MIN_DEG          20      /* 旧默认软件限位；须按新云台实测收窄 */
#define RM_SERVO_MAX_DEG          160     /* 不是舵机机械安全行程 */
#define RM_SERVO_CENTER_DEG       90      /* 旧默认回中指令，需核对协议映射 */
#define RM_SERVO_SLEW_10MS        30      /* 指令角斜率，不代表实物角速度 */
#define RM_SERVO_HOME_SLEW_10MS   10      /* 指令角斜率，不保证机械运动缓慢 */
#define RM_SERVO_HOME_MS          1500    /* 上电回中持续多久，期间不接受新指令 */

/* ==========================================================================
 * 5. 发射机构（PB14 单线 MOS + 软件 PWM 100Hz）
 *    下列时间是旧机构的临时参数。采购表未列波箱/电机型号，也没有机械行程反馈；
 *    实车核对并完成假负载、波形和机构测试前，不代表单发/连发已标定。
 * ========================================================================== */
#define RM_FIRE_PWM_HZ            100
#define RM_FIRE_SOFTSTART_MS      40      /* 软件占空爬升；电流仍需外部测量 */
#define RM_FIRE_SHOT_MS           70      /* 软件定时初值；未按到货波箱标定 */
#define RM_FIRE_BURST_SHOTS       3       /* 软件定时次数；不保证实际发数 */
#define RM_FIRE_BURST_MS          (RM_FIRE_SHOT_MS * RM_FIRE_BURST_SHOTS)
#define RM_FIRE_COOLDOWN_MS       400     /* 软件命令冷却，不是电机热保护 */
#define RM_FIRE_MAX_MS            3000    /* 软件最长通电，不是堵转检测或硬件保护 */
#define RM_FIRE_REVERSE_MS        300
#define RM_FIRE_DUTY_LIMIT        900     /* 连发占空比上限（0~1000 = 90%），单发/三连发走 100% */
#define RM_FIRE_DUTY_PERCENT      (RM_FIRE_DUTY_LIMIT / 10)
#define RM_FIRE_SINGLE_MS         RM_FIRE_SHOT_MS   /* 旧名兼容；仅表示软件定时 */

/* ==========================================================================
 * 6. 通信（蓝牙 USART1）
 * HC-05 出厂 9600，这里保持不变；要让 20Hz 帧 + 10Hz 遥测更宽裕，
 * 可改成 115200（同时用 AT 指令把模块改掉）。
 * ========================================================================== */
#define RM_UART_BAUD_DEFAULT      9600
#define RM_UART_RX_BUF_SIZE       128
#define RM_LINE_BUF_SIZE          48
#define RM_LINK_TIMEOUT_MS        500     /* 蓝牙失联多久自动停车 */
#define RM_LINK_KEEPALIVE_DEFAULT 1
#define RM_TELEM_PERIOD_MS        100
#define RM_TELEM_DEFAULT_ON       1

/* ==========================================================================
 * 7. 裁判系统（本车不接，保持 0；规则允许"自行配备遥控器"）
 * ========================================================================== */
#define RM_ENABLE_REFEREE         0
#define RM_REFEREE_BAUD           115200
#define RM_REFEREE_TX_ENABLE      1
#define RM_REFEREE_HEAT_MARGIN    12
#define RM_REFEREE_HEAT_ENTER     0
#define RM_REFEREE_HP_GUARD       1

/* ==========================================================================
 * 8. 图传键鼠（人工瞄准）链路：USART2 PA2(TX)/PA3(RX)，115200 8N1
 *   本 PCB 的 PA2/PA3 已经给电机 PWM 了，所以这里默认 0（不编译 rm_kbm.c）。
 *   将来要接图传键鼠，两条路：
 *     a) 把电机 PWM 迁到 TIM4（RM_MOTOR_USE_TIM4=1）并飞线 PB6~PB9；
 *     b) 用一块小板子把 PA2/PA3 让出来。
 *   协议三档：0=SIM 自定义帧（用 tools/kbm_sim.ps1 先调通）
 *             1=RAW 把收到的字节打十六进制，用来抓官方协议
 *             2=OFFICIAL 按下面的偏移解析
 * ========================================================================== */
#define RM_ENABLE_KBM             0
#define RM_KBM_PROTO_SIM          0
#define RM_KBM_PROTO_RAW          1
#define RM_KBM_PROTO_OFFICIAL     2
#define RM_KBM_PROTO              0
#define RM_KBM_BAUD               115200
#define RM_KBM_TIMEOUT_MS         300
#define RM_KBM_SOF                0xA5u
#define RM_KBM_CMD                0x40u
#define RM_KBM_RAW_DUMP_LEN       16

#define RM_KBM_OFLEN_MIN          6
#define RM_KBM_OFF_KEY_LO         2
#define RM_KBM_OFF_KEY_HI         3
#define RM_KBM_OFF_MOUSE_X        4
#define RM_KBM_OFF_MOUSE_Y        5
#define RM_KBM_OFF_BUTTON         6

#define RM_KBM_SPEED              320
#define RM_KBM_SNIPE_SPEED        130
#define RM_KBM_ROT                240
#define RM_KBM_SNIPE_ROT          100
#define RM_KBM_PITCH_STEP_DEG10   5
#define RM_KBM_FIRE_HOLD_MS       250

/* ==========================================================================
 * 9. 电池电压检测（可选，需要飞线）
 *   VBAT(2S，最高 8.4V) --- 33k ---+--- PB1(ADC1_IN9)
 *                   |
 *                 10k
 *                   |
 *                  GND
 * 满电时 ADC 约 1.95V。用 1% 电阻，PB1 对地并 100nF，实测校准后再启用。
 * 禁用旧 10k/10k 接法：满电输出 4.2V，超过 ADC 量程，可能损坏 MCU。
 * 默认不启用；BB 响只报警不切断，不能替代低压断电保护。
 * ========================================================================== */
#define RM_ENABLE_VBAT            0
#define RM_VBAT_ADC_CHANNEL       9
#define RM_VBAT_R_TOP             33000UL
#define RM_VBAT_R_BOTTOM          10000UL
#define RM_VBAT_REF_MV            3300UL
#define RM_VBAT_WARN_MV           7200    /* 2S 报警（单节 3.6V） */
#define RM_VBAT_STOP_MV           6800    /* 2S 保护（单节 3.4V） */
#define RM_VBAT_SAMPLE_PERIOD_MS  100

/* ==========================================================================
 * 10. 视觉模块（比赛提供，串口 TTL 直连）
 *   接线：模块 TX  -> PB11 (USART3_RX)
 *         模块 GND -> 板子 GND（必须共地）
 *         模块 5V/VCC -> H1 5V（看模块标称电压，别从 3.3V 取）
 *   波特率/协议以模块为准，默认按 115200 文本行：
 *         a <dx> <dy>\r\n   目标相对画面中心的像素偏差（右正、下正）
 *         t <yaw10> <pitch10>\r\n  目标绝对角度（单位 0.1°）
 *         x\r\n             目标丢失
 *   模块协议不一样的话，只改 rm_vision.c 里的 vision_parse_line() 一处。
 * ========================================================================== */
#define RM_VISION_BAUD            115200
#define RM_VISION_TIMEOUT_MS      400     /* 超过这么久没帧 -> 当作丢目标，保持当前角 */
#define RM_VISION_AIM_DEFAULT     0       /* 上电是否自动瞄准 */
#define RM_VISION_KP_DEG10_PER_PX 3       /* 1 像素偏差 -> 0.3° */
#define RM_VISION_MAX_STEP_DEG10  40      /* 每 10ms 最多动 4°，比舵机斜坡快一点 */
#define RM_VISION_DEADBAND_PX     8       /* 靶心附近不抖 */
#define RM_VISION_YAW_SIGN        1       /* 目标偏右时 Yaw 角度该增大就填 1，反了改 -1 */
#define RM_VISION_PITCH_SIGN      1       /* 目标偏下时 Pitch 角度该减小就填 1，反了改 -1 */

/* ==========================================================================
 * 11. 安全 / 调试
 * ========================================================================== */
#define RM_STARTUP_LOCK_MS        1200    /* 上电锁定；云台仍会请求中心位置 */
#define RM_ENABLE_IWDG            0       /* 比赛前建议改 1；调试时先关 */
#define RM_IWDG_PRESCALER         4
#define RM_IWDG_RELOAD            250     /* 40kHz/64 * 250 ≈ 400ms */

#ifdef __cplusplus
}
#endif

#endif /* __RM_CONFIG_H */
