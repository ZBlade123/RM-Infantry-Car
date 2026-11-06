/**
  ******************************************************************************
  * @file    rm_servo.c
  * @brief   二维云台驱动（v3.1）：Yaw + Pitch
  *
  * 两种硬件，一个宏（RM_GIMBAL_MODE）切换：
  *   1 = PWM 双舵机：Yaw PA8/TIM1_CH1、Pitch PB0/TIM3_CH3，50Hz
  *   2 = 总线舵机：USART3 TX=PB10 115200，帧 #idPpwmTtime!（套件同一套指令）
  *
  * 三条必须做的保护（20KG 舵机堵转 2~3A）：
  *   1) 上电请求中心，1500ms 内不接受指令；无反馈，不保证实际回中速度
  *   2) 正常平滑：3°/10ms = 300°/s
  *   3) 软件限位：Yaw 0~180、Pitch 20~160，撞结构前先停
  *
  * 总线模式发送软件斜坡中间值，T 参数交给舵机内部插值，
  * 每 20ms 最多发送一对变化帧，不读取回包。
  ******************************************************************************
  */

#include "rm_servo.h"
#include "rm_config.h"
#include <stdio.h>

typedef struct
{
    int16_t  tgt10;
    int16_t  cur10;
    uint16_t pulse;
} servo_axis_t;

static servo_axis_t s_yaw;
static servo_axis_t s_pitch;

static uint8_t  s_homing;
static uint32_t s_home_start;

/* ==========================================================================
 * 角度 -> 脉宽（含反向）
 * ========================================================================== */
static uint16_t deg10_to_pulse(int16_t deg10, int16_t min_deg, int16_t max_deg,
                               uint8_t invert, uint16_t pmin, uint16_t pmax)
{
    int32_t deg = (int32_t)deg10 / 10;
    int32_t span = (int32_t)pmax - (int32_t)pmin;
    uint16_t pulse;

    if (deg < (int32_t)min_deg) { deg = (int32_t)min_deg; }
    if (deg > (int32_t)max_deg) { deg = (int32_t)max_deg; }

    pulse = (uint16_t)((int32_t)pmin + (deg * span) / (int32_t)RM_SERVO_SPAN_DEG);
    if (invert != 0u)
    {
        pulse = (uint16_t)((int32_t)pmin + (int32_t)pmax - (int32_t)pulse);
    }
    return pulse;
}

static void yaw_apply(void)
{
    s_yaw.pulse = deg10_to_pulse(s_yaw.cur10,
                                 (int16_t)RM_GIMBAL_YAW_MIN_DEG,
                                 (int16_t)RM_GIMBAL_YAW_MAX_DEG,
                                 (uint8_t)RM_GIMBAL_YAW_INVERT,
                                 (uint16_t)RM_GIMBAL_PULSE_MIN_US,
                                 (uint16_t)RM_GIMBAL_PULSE_MAX_US);
#if (RM_GIMBAL_MODE == RM_GIMBAL_MODE_PWM)
    TIM1->CCR1 = s_yaw.pulse;
#endif
}

static void pitch_apply(void)
{
    s_pitch.pulse = deg10_to_pulse(s_pitch.cur10,
                                   (int16_t)RM_SERVO_MIN_DEG,
                                   (int16_t)RM_SERVO_MAX_DEG,
                                   0u,
                                   (uint16_t)RM_SERVO_PULSE_MIN_US,
                                   (uint16_t)RM_SERVO_PULSE_MAX_US);
#if (RM_GIMBAL_MODE == RM_GIMBAL_MODE_PWM)
    __HAL_TIM_SET_COMPARE(RM_SERVO_HTIM, RM_SERVO_CHANNEL, s_pitch.pulse);
#endif
}

/* ==========================================================================
 * PWM 模式：Yaw 的 TIM1_CH1（50Hz，比较值 = us）
 * Pitch 由 CubeMX 的 TIM3 提供，这里只 start
 * ========================================================================== */
#if (RM_GIMBAL_MODE == RM_GIMBAL_MODE_PWM)
static void yaw_pwm_init(void)
{
    GPIO_InitTypeDef g;

    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    g.Pin   = GPIO_PIN_8;
    g.Mode  = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);

    TIM1->PSC  = 71u;                       /* 72MHz/72 = 1MHz */
    TIM1->ARR  = 19999u;                    /* 1MHz/20000 = 50Hz */
    TIM1->CCR1 = 0u;
    TIM1->CCMR1 = (uint32_t)((6u << 4) | (1u << 3));   /* CH1 PWM1 + 预装载 */
    TIM1->CCER  = TIM_CCER_CC1E;
    TIM1->EGR   = TIM_EGR_UG;
    TIM1->BDTR |= TIM_BDTR_MOE;             /* 高级定时器必须开主输出 */
    TIM1->CR1  |= TIM_CR1_CEN;
}
#endif

/* ==========================================================================
 * 总线模式：USART3 TX only（PB10），115200 8N1
 * 只发不收：云台位置是开环的，读回包等以后要做闭环再说。
 * ========================================================================== */
#if (RM_GIMBAL_MODE == RM_GIMBAL_MODE_BUS)
static UART_HandleTypeDef s_bus;
static uint16_t s_bus_sent_yaw;
static uint16_t s_bus_sent_pitch;
static uint32_t s_bus_last_ms;

static void bus_uart_init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    g.Pin   = GPIO_PIN_10;
    g.Mode  = GPIO_MODE_AF_OD;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);

    s_bus.Instance          = USART3;
    s_bus.Init.BaudRate     = RM_GIMBAL_BUS_BAUD;
    s_bus.Init.WordLength   = UART_WORDLENGTH_8B;
    s_bus.Init.StopBits     = UART_STOPBITS_1;
    s_bus.Init.Parity       = UART_PARITY_NONE;
    s_bus.Init.Mode         = UART_MODE_TX;
    s_bus.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    s_bus.Init.OverSampling = UART_OVERSAMPLING_16;
    (void)HAL_UART_Init(&s_bus);
}

static void bus_send(uint8_t id, uint16_t pulse, uint16_t time_ms)
{
    char buf[24];
    int  n;

    n = sprintf(buf, "#%03uP%04uT%04u!", (unsigned)id,
                (unsigned)pulse, (unsigned)time_ms);
    if (n > 0)
    {
        (void)HAL_UART_Transmit(&s_bus, (uint8_t *)buf, (uint16_t)n, 20u);
    }
}

/* 原样发一串（改舵机 ID、设偏差等维护指令用，例如 "#255PID001!"） */
void Gimbal_BusRaw(const char *s)
{
    uint8_t n = 0u;

    if (s == 0)
    {
        return;
    }
    while ((s[n] != '\0') && (n < 23u))
    {
        n++;
    }
    if (n != 0u)
    {
        (void)HAL_UART_Transmit(&s_bus, (uint8_t *)s, n, 20u);
    }
}
#else
void Gimbal_BusRaw(const char *s)
{
    (void)s;
}
#endif

/* ==========================================================================
 * 初始化
 * ========================================================================== */
void Servo_Init(void)
{
    s_homing     = 1u;
    s_home_start = HAL_GetTick();

    /* No position feedback: request center without an artificial end-stop sweep. */
    s_yaw.cur10   = (int16_t)(RM_GIMBAL_YAW_CENTER_DEG * 10);
    s_yaw.tgt10   = (int16_t)(RM_GIMBAL_YAW_CENTER_DEG * 10);
    s_yaw.pulse   = 0u;
    s_pitch.cur10 = (int16_t)(RM_SERVO_CENTER_DEG * 10);
    s_pitch.tgt10 = (int16_t)(RM_SERVO_CENTER_DEG * 10);
    s_pitch.pulse = 0u;

#if (RM_GIMBAL_MODE == RM_GIMBAL_MODE_PWM)
    (void)HAL_TIM_PWM_Start(RM_SERVO_HTIM, RM_SERVO_CHANNEL);
    yaw_pwm_init();
#else
    bus_uart_init();
    s_bus_sent_yaw   = 0u;
    s_bus_sent_pitch = 0u;
    s_bus_last_ms    = 0u;
#endif

    yaw_apply();
    pitch_apply();
}

/* ==========================================================================
 * 目标设定（回中阶段先忽略）
 * ========================================================================== */
void Servo_SetTargetDeg10(int16_t deg_x10)
{
    if (s_homing != 0u)
    {
        return;
    }
    if (deg_x10 < (int16_t)(RM_SERVO_MIN_DEG * 10)) { deg_x10 = (int16_t)(RM_SERVO_MIN_DEG * 10); }
    if (deg_x10 > (int16_t)(RM_SERVO_MAX_DEG * 10)) { deg_x10 = (int16_t)(RM_SERVO_MAX_DEG * 10); }
    s_pitch.tgt10 = deg_x10;
}

void Servo_SetTargetDeg(int16_t deg)
{
    Servo_SetTargetDeg10((int16_t)(deg * 10));
}

void Yaw_SetTargetDeg10(int16_t deg_x10)
{
    if (s_homing != 0u)
    {
        return;
    }
    if (deg_x10 < (int16_t)(RM_GIMBAL_YAW_MIN_DEG * 10)) { deg_x10 = (int16_t)(RM_GIMBAL_YAW_MIN_DEG * 10); }
    if (deg_x10 > (int16_t)(RM_GIMBAL_YAW_MAX_DEG * 10)) { deg_x10 = (int16_t)(RM_GIMBAL_YAW_MAX_DEG * 10); }
    s_yaw.tgt10 = deg_x10;
}

void Gimbal_SetDeg10(int16_t yaw_x10, int16_t pitch_x10)
{
    Yaw_SetTargetDeg10(yaw_x10);
    Servo_SetTargetDeg10(pitch_x10);
}

uint8_t Servo_IsHoming(void)
{
    return s_homing;
}

/* ==========================================================================
 * 10ms 任务
 * ========================================================================== */
static void axis_slew(servo_axis_t *ax, int16_t step, uint8_t homing)
{
    int32_t diff;

    if (homing != 0u)
    {
        ax->tgt10 = (int16_t)(RM_SERVO_CENTER_DEG * 10);   /* Pitch 中位 */
    }

    diff = (int32_t)ax->tgt10 - (int32_t)ax->cur10;
    if (diff >  (int32_t)step)      { ax->cur10 = (int16_t)(ax->cur10 + step); }
    else if (diff < -(int32_t)step) { ax->cur10 = (int16_t)(ax->cur10 - step); }
    else                            { ax->cur10 = ax->tgt10; }
}

void Servo_Task(void)
{
    uint32_t now = HAL_GetTick();
    int16_t  step;

    if (s_homing != 0u)
    {
        step = (int16_t)RM_GIMBAL_HOME_SLEW_10MS;
        if ((now - s_home_start) >= RM_GIMBAL_HOME_MS)
        {
            s_homing = 0u;
            step = (int16_t)RM_GIMBAL_SLEW_10MS;
        }
        /* 回中阶段两轴都往中心走（Yaw 中心单独算） */
        s_yaw.tgt10 = (int16_t)(RM_GIMBAL_YAW_CENTER_DEG * 10);
        axis_slew(&s_pitch, step, 0u);
    }
    else
    {
        step = (int16_t)RM_GIMBAL_SLEW_10MS;
        axis_slew(&s_pitch, step, 0u);
    }

    /* Yaw 单独走，因为中位不同 */
    {
        int32_t diff = (int32_t)s_yaw.tgt10 - (int32_t)s_yaw.cur10;
        if (diff >  (int32_t)step)      { s_yaw.cur10 = (int16_t)(s_yaw.cur10 + step); }
        else if (diff < -(int32_t)step) { s_yaw.cur10 = (int16_t)(s_yaw.cur10 - step); }
        else                            { s_yaw.cur10 = s_yaw.tgt10; }
    }

#if (RM_GIMBAL_MODE == RM_GIMBAL_MODE_PWM)
    yaw_apply();
    pitch_apply();
#else
    yaw_apply();
    pitch_apply();
    /* Transmit the slew-limited command, not the final target. */
    if ((now - s_bus_last_ms) >= 20u)
    {
        uint16_t py = deg10_to_pulse(s_yaw.cur10,
                                     (int16_t)RM_GIMBAL_YAW_MIN_DEG,
                                     (int16_t)RM_GIMBAL_YAW_MAX_DEG,
                                     (uint8_t)RM_GIMBAL_YAW_INVERT,
                                     (uint16_t)RM_GIMBAL_PULSE_MIN_US,
                                     (uint16_t)RM_GIMBAL_PULSE_MAX_US);
        uint16_t pp = deg10_to_pulse(s_pitch.cur10,
                                     (int16_t)RM_SERVO_MIN_DEG,
                                     (int16_t)RM_SERVO_MAX_DEG,
                                     0u,
                                     (uint16_t)RM_SERVO_PULSE_MIN_US,
                                     (uint16_t)RM_SERVO_PULSE_MAX_US);
        if ((py != s_bus_sent_yaw) || (pp != s_bus_sent_pitch))
        {
            uint16_t move_ms = (s_homing != 0u) ? (uint16_t)RM_GIMBAL_HOME_MS
                                                       : (uint16_t)RM_GIMBAL_BUS_TIME_MS;
            bus_send((uint8_t)RM_GIMBAL_BUS_YAW_ID,   py, move_ms);
            bus_send((uint8_t)RM_GIMBAL_BUS_PITCH_ID, pp, move_ms);
            s_bus_sent_yaw   = py;
            s_bus_sent_pitch = pp;
        }
        s_bus_last_ms = now;
    }
#endif
}

/* ==========================================================================
 * 读取
 * ========================================================================== */
int16_t Servo_GetDeg10(void)
{
    return s_pitch.cur10;
}

int16_t Servo_GetTargetDeg10(void)
{
    return s_pitch.tgt10;
}

uint16_t Servo_GetPulseUs(void)
{
    return s_pitch.pulse;
}

int16_t Yaw_GetDeg10(void)
{
    return s_yaw.cur10;
}

int16_t Yaw_GetTargetDeg10(void)
{
    return s_yaw.tgt10;
}

uint16_t Yaw_GetPulseUs(void)
{
    return s_yaw.pulse;
}
