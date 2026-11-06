/**
  ******************************************************************************
  * @file    rm_motor.c
  * @brief   电机驱动层实现（旧板级映射：F103 + TB6612；实物版本须逐点核对）
  *
  * 接线（按 PCB 网络表，详见 docs/引脚与接线.md）：
  *   轮 0 FL: PWM = TIM2_CH2 (PA1)   方向 = PA4(IN1) / PA5(IN2)
  *   轮 1 RL: PWM = TIM2_CH3 (PA2)   方向 = PA6(IN1) / PA7(IN2)
  *   轮 2 RR: PWM = TIM2_CH1 (PA0)   方向由 rm_config.h 指定（总线版 IN1=PA15）
  *   轮 3 FR: PWM = TIM2_CH4 (PA3)   方向 = PB12(IN1) / PB13(IN2)
  *   TIM2: PSC=71, ARR=999 -> 72MHz/72/1000 = 1kHz，比较值 = 占空比‰
  *   （若改 RM_MOTOR_USE_TIM4=1，则 4 路 PWM 换成 TIM4 的 PB6~PB9，其余不变）
  *
  * 限幅与斜坡仅用于减小指令突变，不是电流限制或过流保护。
  * 当前采购表未给电机堵转电流；启动/堵转电流及驱动温升必须按实物核对。
  *
  * 死区是可调的开环起动参数，需根据实物低速测试调整。
  ******************************************************************************
  */

#include "rm_motor.h"
#include "rm_config.h"

typedef struct
{
    GPIO_TypeDef *in1_port;
    uint16_t      in1_pin;
    GPIO_TypeDef *in2_port;
    uint16_t      in2_pin;
    uint32_t      tim_channel;
} motor_hw_t;

/* 物理接线表：顺序必须和 rm_wheel_t 一致（0=FL 1=RL 2=RR 3=FR） */
static const motor_hw_t s_hw[RM_MOTOR_COUNT] =
{
    { RM_DIR_FL_PORT, RM_DIR_FL_IN1, RM_DIR_FL_PORT, RM_DIR_FL_IN2, TIM_CHANNEL_2 }, /* FL -> PA1 */
    { RM_DIR_RL_PORT, RM_DIR_RL_IN1, RM_DIR_RL_PORT, RM_DIR_RL_IN2, TIM_CHANNEL_3 }, /* RL -> PA2 */
    { RM_DIR_RR_PORT, RM_DIR_RR_IN1, RM_DIR_RR_PORT2, RM_DIR_RR_IN2, TIM_CHANNEL_1 }, /* RR -> PA0 */
    { RM_DIR_FR_PORT, RM_DIR_FR_IN1, RM_DIR_FR_PORT, RM_DIR_FR_IN2, TIM_CHANNEL_4 }  /* FR -> PA3 */
};

static int8_t  s_sign[RM_MOTOR_COUNT] = RM_MOTOR_FWD_SIGN;
static int16_t s_trim[RM_MOTOR_COUNT] = RM_MOTOR_TRIM;

static int16_t  s_target[RM_MOTOR_COUNT];
static int16_t  s_out[RM_MOTOR_COUNT];
static uint32_t s_boot_ms;

/* ==========================================================================
 * PWM 定时器（TIM4，PB6~PB9）
 * ========================================================================== */
#if (RM_MOTOR_USE_TIM4 != 0)
static void motor_pwm_init(void)
{
    GPIO_InitTypeDef g;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    /* PB6=CH1 PB7=CH2 PB8=CH3 PB9=CH4，AF 推挽 */
    g.Pin   = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
    g.Mode  = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);

    /* 把 PA0~PA3 从 CubeMX 配的 TIM2 复用上摘下来，否则会和 USART2 抢脚 */
    g.Pin   = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    TIM4->PSC  = 71u;
    TIM4->ARR  = 999u;
    TIM4->CCR1 = 0u;
    TIM4->CCR2 = 0u;
    TIM4->CCR3 = 0u;
    TIM4->CCR4 = 0u;
    TIM4->CCMR1 = (uint32_t)((6u << 4) | (1u << 3) | (6u << 12) | (1u << 11));
    TIM4->CCMR2 = (uint32_t)((6u << 4) | (1u << 3) | (6u << 12) | (1u << 11));
    TIM4->CCER  = 0x1111u;
    TIM4->EGR   = TIM_EGR_UG;
    TIM4->CR1  |= TIM_CR1_CEN;
}

static void motor_set_compare(uint32_t channel, uint16_t pwm)
{
    switch (channel)
    {
        case TIM_CHANNEL_1: TIM4->CCR1 = pwm; break;
        case TIM_CHANNEL_2: TIM4->CCR2 = pwm; break;
        case TIM_CHANNEL_3: TIM4->CCR3 = pwm; break;
        case TIM_CHANNEL_4: TIM4->CCR4 = pwm; break;
        default: break;
    }
}
#else
static void motor_pwm_init(void)
{
#if (RM_RR_DIR_ALT != 0)
    GPIO_InitTypeDef g;

    /* 总线云台占了 PB10/PB11：RR 方向脚改到 PA15/PB1，PA15 需先关 JTAG 留 SWD */
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    g.Pin   = RM_DIR_RR_IN1;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RM_DIR_RR_PORT, &g);

    g.Pin   = RM_DIR_RR_IN2;
    HAL_GPIO_Init(RM_DIR_RR_PORT2, &g);
#endif

    (void)HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    (void)HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
    (void)HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    (void)HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
}
#endif

/* ==========================================================================
 * 内部：把一路电机的逻辑输出写到硬件
 * ========================================================================== */
static void motor_apply(uint8_t idx, int16_t logical)
{
    int32_t v;
    int16_t phys;
    uint16_t pwm;

    v = (int32_t)logical + (int32_t)s_trim[idx];
    if (v >  (int32_t)RM_MOTOR_LIMIT) { v =  (int32_t)RM_MOTOR_LIMIT; }
    if (v < -(int32_t)RM_MOTOR_LIMIT) { v = -(int32_t)RM_MOTOR_LIMIT; }

    phys = (int16_t)(v * (int32_t)s_sign[idx]);
    pwm  = (uint16_t)((phys >= 0) ? phys : -phys);

    if (pwm < (uint16_t)RM_MOTOR_DEADZONE)
    {
        pwm = 0u;
    }
    if (pwm > (uint16_t)RM_MOTOR_LIMIT)
    {
        pwm = (uint16_t)RM_MOTOR_LIMIT;
    }

    if (pwm == 0u)
    {
        /* TB6612: IN1=IN2=0 -> 短路刹车还是滑行取决于桥，这里按滑行接 */
        HAL_GPIO_WritePin(s_hw[idx].in1_port, s_hw[idx].in1_pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(s_hw[idx].in2_port, s_hw[idx].in2_pin, GPIO_PIN_RESET);
    }
    else if (phys > 0)
    {
        HAL_GPIO_WritePin(s_hw[idx].in1_port, s_hw[idx].in1_pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(s_hw[idx].in2_port, s_hw[idx].in2_pin, GPIO_PIN_RESET);
    }
    else
    {
        HAL_GPIO_WritePin(s_hw[idx].in1_port, s_hw[idx].in1_pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(s_hw[idx].in2_port, s_hw[idx].in2_pin, GPIO_PIN_SET);
    }

#if (RM_MOTOR_USE_TIM4 != 0)
    motor_set_compare(s_hw[idx].tim_channel, pwm);
#else
    __HAL_TIM_SET_COMPARE(&htim2, s_hw[idx].tim_channel, pwm);
#endif
}

/* ==========================================================================
 * 初始化
 * ========================================================================== */
void Motor_Init(void)
{
    uint8_t i;

    for (i = 0u; i < RM_MOTOR_COUNT; i++)
    {
        s_target[i] = 0;
        s_out[i]    = 0;
        motor_apply(i, 0);
    }

    motor_pwm_init();
    s_boot_ms = HAL_GetTick();

    for (i = 0u; i < RM_MOTOR_COUNT; i++)
    {
        motor_apply(i, 0);
    }
}

/* ==========================================================================
 * 目标设定
 * ========================================================================== */
void Motor_SetTarget(uint8_t idx, int16_t logical)
{
    if (idx >= RM_MOTOR_COUNT)
    {
        return;
    }
    if (logical >  RM_PWM_MAX) { logical =  RM_PWM_MAX; }
    if (logical < -RM_PWM_MAX) { logical = -RM_PWM_MAX; }
    s_target[idx] = logical;
}

void Motor_StopAll(void)
{
    uint8_t i;
    for (i = 0u; i < RM_MOTOR_COUNT; i++)
    {
        s_target[i] = 0;
    }
}

void Motor_EmergencyStop(void)
{
    uint8_t i;
    for (i = 0u; i < RM_MOTOR_COUNT; i++)
    {
        s_target[i] = 0;
        s_out[i]    = 0;
        motor_apply(i, 0);
    }
}

/* ==========================================================================
 * 10ms 任务：斜坡逼近
 *   上电前 300ms 用 RM_MOTOR_SOFT_RAMP_STEP，避免一上电就大电流冲击
 * ========================================================================== */
void Motor_Task(void)
{
    uint8_t i;
    int32_t diff;
    int16_t step = (int16_t)RM_MOTOR_RAMP_STEP;

    if ((HAL_GetTick() - s_boot_ms) < 300u)
    {
        step = (int16_t)RM_MOTOR_SOFT_RAMP_STEP;
    }

    for (i = 0u; i < RM_MOTOR_COUNT; i++)
    {
        diff = (int32_t)s_target[i] - (int32_t)s_out[i];

        if (diff >  (int32_t)step)
        {
            s_out[i] = (int16_t)(s_out[i] + step);
        }
        else if (diff < -(int32_t)step)
        {
            s_out[i] = (int16_t)(s_out[i] - step);
        }
        else
        {
            s_out[i] = s_target[i];
        }

        motor_apply(i, s_out[i]);
    }
}

/* ==========================================================================
 * 标定接口
 * ========================================================================== */
uint8_t Motor_IsValidIdx(uint8_t idx)
{
    return (idx < RM_MOTOR_COUNT) ? 1u : 0u;
}

int16_t Motor_GetTarget(uint8_t idx)
{
    return (idx < RM_MOTOR_COUNT) ? s_target[idx] : 0;
}

int16_t Motor_GetOutput(uint8_t idx)
{
    return (idx < RM_MOTOR_COUNT) ? s_out[idx] : 0;
}

int8_t Motor_GetSign(uint8_t idx)
{
    return (idx < RM_MOTOR_COUNT) ? s_sign[idx] : 0;
}

int16_t Motor_GetTrim(uint8_t idx)
{
    return (idx < RM_MOTOR_COUNT) ? s_trim[idx] : 0;
}

void Motor_FlipSign(uint8_t idx)
{
    if (idx < RM_MOTOR_COUNT)
    {
        s_sign[idx] = (int8_t)(-s_sign[idx]);
    }
}

void Motor_SetTrim(uint8_t idx, int16_t trim)
{
    if (idx < RM_MOTOR_COUNT)
    {
        if (trim >  (int16_t)RM_MOTOR_TRIM_MAX) { trim =  (int16_t)RM_MOTOR_TRIM_MAX; }
        if (trim < (int16_t)RM_MOTOR_TRIM_MIN) { trim = (int16_t)RM_MOTOR_TRIM_MIN; }
        s_trim[idx] = trim;
    }
}
