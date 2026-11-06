/**
  ******************************************************************************
  * @file    rm_shooter.c
  * @brief   发射机构实现
  *
  * 硬件现状：开源板只给了 PB14 一根线控制波箱 MOS，只能单向通电。
  *   - 单发：通电 RM_FIRE_SHOT_MS（一个活塞循环）后断开
  *   - 三连发：通 3 个循环；AEG 波箱断电单发常停在半压缩位，连打才可靠
  *   - 连发：一直通电，但超过 RM_FIRE_MAX_MS 自动断（防堵转烧管）
  *   - 冷却：单发/三连发打完锁 RM_FIRE_COOLDOWN_MS，防 20Hz 指令刷成连发
  *   - 软启动：占空比从 0 升到 100%（RM_FIRE_SOFTSTART_MS），
  *          必须短于一个活塞循环，否则永远到不了全占空、打不响
  *   - 卡弹：没有电流反馈就没有真正的自动检测，这里给"反转"指令兜底；
  *          如果把 PB15 接到驱动板第二路输入，把 RM_FIRE_IN2_ENABLE 改成 1
  *          就能真的反转把弹拨回去。
  *
  * 软件 PWM：1ms 节拍，100Hz 时每周期 10 个时隙，占空比分辨率约 10%，
  *   对波箱电机足够了，不用为了它再占一个定时器。
  ******************************************************************************
  */

#include "rm_shooter.h"
#include "rm_config.h"

#if (RM_FIRE_PWM_HZ < 1) || (RM_FIRE_PWM_HZ > 1000)
#error "RM_FIRE_PWM_HZ must be between 1 and 1000"
#elif ((1000 % RM_FIRE_PWM_HZ) != 0)
#error "RM_FIRE_PWM_HZ must divide the 1kHz software tick rate"
#endif
#define FIRE_PWM_TICKS (1000u / RM_FIRE_PWM_HZ)

static uint8_t  s_mode;         /* 0 停 / 1 单发 / 2 连发 / 3 三连发 */
static uint8_t  s_state;        /* rm_fire_state_t */
static uint8_t  s_duty;         /* 0 ~ 100 */
static uint16_t s_pwm_cnt;     /* 1ms slots within one PWM period */
static int16_t  s_dir;          /* +1 正转 / -1 反转 */
static uint32_t s_deadline;
static uint32_t s_lock_until;   /* 单发/三连发打完的冷却截止时刻 */

/* ==========================================================================
 * 输出
 * ========================================================================== */
static void fire_dir_apply(void)
{
#if (RM_FIRE_IN2_ENABLE != 0)
    if (s_dir >= 0)
    {
        HAL_GPIO_WritePin(RM_FIRE_PORT, RM_FIRE_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(RM_FIRE_IN2_PORT, RM_FIRE_IN2_PIN, GPIO_PIN_RESET);
    }
    else
    {
        HAL_GPIO_WritePin(RM_FIRE_PORT, RM_FIRE_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(RM_FIRE_IN2_PORT, RM_FIRE_IN2_PIN, GPIO_PIN_SET);
    }
#else
    /* 只有一根线：反转指令 = 断电，至少不会发热 */
    if (s_dir >= 0)
    {
        HAL_GPIO_WritePin(RM_FIRE_PORT, RM_FIRE_PIN, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(RM_FIRE_PORT, RM_FIRE_PIN, GPIO_PIN_RESET);
    }
#endif
}

static void fire_output_off(void)
{
    HAL_GPIO_WritePin(RM_FIRE_PORT, RM_FIRE_PIN, GPIO_PIN_RESET);
#if (RM_FIRE_IN2_ENABLE != 0)
    HAL_GPIO_WritePin(RM_FIRE_IN2_PORT, RM_FIRE_IN2_PIN, GPIO_PIN_RESET);
#endif
}

/* ==========================================================================
 * 初始化
 * ========================================================================== */
void Shooter_Init(void)
{
    s_mode     = 0u;
    s_state    = (uint8_t)RM_FIRE_IDLE;
    s_duty     = 0u;
    s_pwm_cnt  = 0u;
    s_dir      = 1;
    s_deadline = 0u;
    s_lock_until = 0u;

#if (RM_FIRE_IN2_ENABLE != 0)
    {
        GPIO_InitTypeDef g;

        g.Pin   = RM_FIRE_IN2_PIN;
        g.Mode  = GPIO_MODE_OUTPUT_PP;
        g.Pull  = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(RM_FIRE_IN2_PORT, &g);
    }
#endif

    fire_output_off();
}

/* ==========================================================================
 * 指令
 * ========================================================================== */
void Shooter_Stop(void)
{
    s_mode  = 0u;
    s_state = (uint8_t)RM_FIRE_IDLE;
    s_duty  = 0u;
    fire_output_off();
}

void Shooter_SetMode(uint8_t mode)
{
    uint32_t now = HAL_GetTick();

    if (mode == 0u)
    {
        Shooter_Stop();
        return;
    }

    if (mode > 3u)
    {
        return;
    }

    /* 冷却期内不接受单发/三连发：电机刚停还没停稳就再通电，活塞容易卡半路。
       连发(2)不受限——按住不放本来就是要它一直转。 */
    if ((mode != 2u) && ((int32_t)(now - s_lock_until) < 0))
    {
        return;
    }

    /* 正在动作时忽略新指令，避免 20Hz 上位机把单发刷成连发 */
    if (s_state != (uint8_t)RM_FIRE_IDLE)
    {
        return;
    }

    s_mode  = mode;
    s_state = (uint8_t)RM_FIRE_FIRING;
    s_dir   = 1;

    if (mode == 2u)
    {
        s_deadline = now + RM_FIRE_MAX_MS;          /* 从按下开始计时，不刷新 */
    }
    else if (mode == 3u)
    {
        s_deadline = now + RM_FIRE_BURST_MS;
    }
    else
    {
        s_deadline = now + RM_FIRE_SHOT_MS;
    }

    fire_dir_apply();
}

void Shooter_Reverse(void)
{
    s_state    = (uint8_t)RM_FIRE_REVERSE;
    s_dir      = -1;
    s_deadline = HAL_GetTick() + RM_FIRE_REVERSE_MS;
    s_mode     = 0u;
    fire_dir_apply();
}

/* ==========================================================================
 * 1ms 软件 PWM
 * ========================================================================== */
void Shooter_PwmTick(void)
{
    s_pwm_cnt++;
    if (s_pwm_cnt >= FIRE_PWM_TICKS)
    {
        s_pwm_cnt = 0u;
    }

    if ((s_state == (uint8_t)RM_FIRE_IDLE) || (s_duty == 0u))
    {
        fire_output_off();
    }
    else if (((uint32_t)s_pwm_cnt * 100u) < ((uint32_t)s_duty * FIRE_PWM_TICKS))
    {
        fire_dir_apply();
    }
    else
    {
        fire_output_off();
    }
}

/* ==========================================================================
 * 10ms 状态机
 * ========================================================================== */
void Shooter_Task(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t  target_duty;
    uint8_t  step;

    /* 超时保护 */
    if ((s_state == (uint8_t)RM_FIRE_FIRING) || (s_state == (uint8_t)RM_FIRE_REVERSE))
    {
        if ((int32_t)(now - s_deadline) >= 0)
        {
            uint8_t was = s_mode;

            s_state = (uint8_t)RM_FIRE_IDLE;
            s_mode  = 0u;
            s_duty  = 0u;
            fire_output_off();

            /* 发射结束后进入冷却（反转 was==0 不冷却） */
            if (was != 0u)
            {
                s_lock_until = now + RM_FIRE_COOLDOWN_MS;
            }
        }
    }

    /* 软启动 / 关断斜坡 */
    step = (uint8_t)(100u / (RM_FIRE_SOFTSTART_MS / 10u));
    if (step == 0u)
    {
        step = 10u;
    }

    /* 只有连发限占空比（90%），单发/三连发/反转要给足力气 */
    target_duty = (s_state == (uint8_t)RM_FIRE_IDLE) ? 0u
                : ((s_mode == 2u) ? (uint8_t)RM_FIRE_DUTY_PERCENT : 100u);

    if (s_duty < target_duty)
    {
        s_duty = (uint8_t)((s_duty + step > target_duty) ? target_duty : (s_duty + step));
    }
    else if (s_duty > target_duty)
    {
        uint8_t down = (uint8_t)(step * 3u);
        s_duty = (s_duty < down) ? (uint8_t)0 : (uint8_t)(s_duty - down);
    }
    else
    {
        /* 不变 */
    }
}

/* ==========================================================================
 * 查询
 * ========================================================================== */
uint8_t Shooter_GetMode(void)
{
    return s_mode;
}

uint8_t Shooter_GetState(void)
{
    return s_state;
}

uint8_t Shooter_GetDuty(void)
{
    return s_duty;
}
