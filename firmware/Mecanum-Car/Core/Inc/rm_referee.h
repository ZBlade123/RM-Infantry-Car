/**
  ******************************************************************************
  * @file    rm_referee.h
  * @brief   RoboMaster 裁判系统协议解析（纯协议层，不绑定 UART）
  *
  * 参考：2026_new_sentry-main/stm32_ws/Devices/Src/referee_protocol.[ch]
  *       （作者把"协议事实"和"UART 绑定"分开，我们沿用这个边界）
  *
  * 为什么单独做一层：
  *   1) 裁判系统是官方输出电压/热量/血量/弹量/键鼠数据的唯一来源，
  *      校内赛"完整形态"考核要看这些数据，比赛时还要用它做热量保护；
  *   2) 协议层不依赖 HAL，可以在 PC 上用 gcc 跑单元测试（tools/hosttest/），
  *      硬件还没接上就能先验证解析对不对；
  *   3) 以后接 UART 只需要在中断里调 Referee_Feed()，协议层不用动。
  *
  * 串口冲突提醒（重要）：
  *   USART1(PA9/PA10) 已经给蓝牙用。裁判系统要再占一个 UART，
  *   而 USART2 的 PA2/PA3 是 TIM2_CH3/CH4(PWM)，USART3 的 PB10/PB11 是 RR 轮方向脚。
  *   接入方案（改接线，不改代码逻辑）：
  *     - RR 方向脚移到 PA8/PA11，FR 方向脚保留 → 空出 PB10/PB11 给 USART3；
  *     - 或把 PWM 从 TIM2 换到 TIM1(PA8~PA11) 的通道，把 PA2/PA3 让给 USART2。
  *   确定方案后：改 rm_motor.c 的 s_hw[] 表 + CubeMX 重配 + 打开 RM_ENABLE_REFEREE。
  ******************************************************************************
  */

#ifndef __RM_REFEREE_H
#define __RM_REFEREE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ==========================================================================
 * 协议常量（以官方《裁判系统串口协议》为准）
 * ========================================================================== */
#define RM_REFEREE_SOF              0xA5U
#define RM_REFEREE_FRAME_MAX        192U   /* 官方最大帧长 */
#define RM_REFEREE_HEADER_SIZE      5U     /* SOF + data_len(2) + seq + crc8 */
#define RM_REFEREE_CRC8_INIT        0xFFU
#define RM_REFEREE_CRC16_INIT       0xFFFFU
#define RM_REFEREE_PAYLOAD_MAX      16U    /* 原始数据段缓存长度 */

/* 在线判定超时：裁判系统通常 50Hz~100Hz 发帧，1s 没收到基本就是线没接/没供电 */
#ifndef RM_REFEREE_TIMEOUT_MS
#define RM_REFEREE_TIMEOUT_MS       1000u
#endif

/* 命令 ID（只列本工程用得到的） */
#define RM_REF_CMD_GAME_STATUS      0x0001U
#define RM_REF_CMD_GAME_ROBOT_HP    0x0003U
#define RM_REF_CMD_ROBOT_STATUS     0x0201U
#define RM_REF_CMD_POWER_HEAT       0x0202U
#define RM_REF_CMD_SHOOT_DATA       0x0207U
#define RM_REF_CMD_PROJECTILE       0x0208U
#define RM_REF_CMD_ROBOT_INTERACT   0x0301U
#define RM_REF_CMD_CUSTOM_CONTROL   0x0304U

/* 有效位 */
#define RM_REFEREE_VALID_GAME       0x01U
#define RM_REFEREE_VALID_HP         0x02U
#define RM_REFEREE_VALID_ROBOT      0x04U
#define RM_REFEREE_VALID_POWER      0x08U
#define RM_REFEREE_VALID_SHOOT      0x10U
#define RM_REFEREE_VALID_ALLOW      0x20U
#define RM_REFEREE_VALID_RAW        0x40U

/* ==========================================================================
 * 状态缓存
 * ========================================================================== */
typedef struct
{
    uint8_t  valid_mask;        /* RM_REFEREE_VALID_* 位图 */
    uint8_t  online;            /* 1 = 至少收到过一帧合法数据 */
    uint32_t last_rx_ms;        /* 最近一次合法帧时间戳 */
    uint16_t last_cmd;          /* 最近一次合法帧命令 ID */
    uint8_t  last_seq;          /* 最近一次合法帧序号 */

    /* 0x0001 比赛状态 */
    uint8_t  game_type;         /* 低 4 位比赛类型 */
    uint8_t  game_progress;     /* 高 4 位当前阶段 */
    uint16_t stage_remain_s;    /* 当前阶段剩余秒数 */

    /* 0x0003 全场血量（红1..红7, 红前哨, 红基地, 蓝1..蓝7, 蓝前哨, 蓝基地） */
    uint16_t robot_hp[16];

    /* 0x0201 本机器人状态 */
    uint8_t  robot_id;
    uint8_t  robot_level;
    uint16_t hp;
    uint16_t max_hp;
    uint16_t heat_limit;        /* 枪口热量上限 */
    uint16_t cooling_rate;      /* 枪口热量冷却值（每秒） */
    uint16_t chassis_power_limit; /* 底盘功率上限，单位 W */
    uint8_t  power_out_bits;    /* bit0 gimbal, bit1 chassis, bit2 shooter 的 24V 输出状态 */

    /* 0x0202 功率热量 */
    uint16_t buffer_energy;     /* 缓冲能量，单位 J */
    uint16_t heat_17mm_1;
    uint16_t heat_17mm_2;
    uint16_t heat_42mm;

    /* 0x0207 发射数据 */
    uint8_t  bullet_type;
    uint8_t  shooter_number;
    uint8_t  launching_freq;    /* 射频，单位 Hz */
    float    initial_speed;     /* 弹丸初速度，单位 m/s */

    /* 0x0208 弹丸允许量 */
    uint16_t allow_17mm;
    uint16_t allow_42mm;
    uint16_t allow_fortress;
    uint16_t gold_coin;

    /* 诊断 */
    uint16_t frame_count;
    uint16_t crc_error_count;
    uint16_t len_error_count;
    uint16_t overflow_count;
    uint8_t  frame_be;          /* 1=裁判系统的 CRC16 是大端（小端为默认） */

    /* 最近一帧原始数据段（调试用：接上硬件后先打印出来逐字段核对手册） */
    uint16_t raw_cmd;
    uint8_t  raw_len;
    uint8_t  raw[RM_REFEREE_PAYLOAD_MAX];
} rm_referee_status_t;

/* ==========================================================================
 * 接口
 * ========================================================================== */
void                    Referee_Init(void);
void                    Referee_Reset(void);

/* 喂入任意长度的字节流（在 UART 接收中断 / DMA idle 回调里调用） */
void                    Referee_Feed(const uint8_t *data, uint16_t len, uint32_t now_ms);

const rm_referee_status_t *Referee_Get(void);

/* 在线判断：收到过合法帧且距上次不超过 timeout_ms */
uint8_t                 Referee_IsOnline(uint32_t now_ms);

/* 构造一帧（将来发 UI / 0x0301 交互数据用；自检也用它） */
uint16_t                Referee_BuildFrame(uint8_t *out,
                                           uint16_t cmd_id,
                                           const uint8_t *payload,
                                           uint16_t payload_len);

/* 不接硬件自检：内部构造 0x0001 + 0x0201 两帧喂进解析器，校验结果是否正确。
 * 返回 1 通过，0 失败。详细错误码看返回值即可，也可用串口指令看 Referee_Get()。 */
uint8_t                 Referee_SelfTest(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* __RM_REFEREE_H */
