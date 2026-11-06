/**
  ******************************************************************************
  * @file    rm_chassis.c
  * @brief   麦轮底盘解算（v2.0）
  *
  * 轮子编号与位置（车头朝上俯视）：
  *      FL(0)          FR(3)
  *      RL(1)          RR(2)
  *
  * X 型麦轮逆运动学（vx 前进、vy 左移、wz 逆时针为正）：
  *   wFL = vx - vy - wz
  *   wRL = vx + vy - wz
  *   wRR = vx + vy + wz
  *   wFR = vx - vy + wz
  * 这里 wz 的系数用 "k = 轮距/2 + 轴距/2" 归一化到和 vx/vy 同一个 0~1000 尺度，
  * 保证"原地旋转"和"直行"的占空比可比。
  *
  * 这一层做三件事（都是为了保护 TB6612 和手感）：
  *   1) 指令变化率限制：点一下键盘不会瞬间冲出去；
  *   2) 电池补偿（可选）：电压掉了按比例补占空比，手感不变；
  *   3) 逆解后等比归一化，任何方向组合都不会让某个轮子超限。
  ******************************************************************************
  */

#include "rm_chassis.h"
#include "rm_config.h"
#include "rm_motor.h"
#include <math.h>

static int16_t s_vx;
static int16_t s_vy;
static int16_t s_wz;

static int16_t s_cvx;      /* 斜坡后的实际指令 */
static int16_t s_cvy;
static int16_t s_cwz;

static uint8_t s_follow;        /* 1 = 云台系驱动 */
static int16_t s_yaw10;         /* 云台当前角 x10，由应用层每 10ms 喂 */

static int16_t s_batt_mv;  /* 0 = 电压未知，不补偿 */

static uint8_t s_test_mode;
static uint8_t s_test_idx;
static int16_t s_test_spd;

/* ==========================================================================
 * 内部：按每 10ms 的步长逼近目标
 * ========================================================================== */
static int16_t ramp_toward(int16_t cur, int16_t target)
{
    int32_t diff = (int32_t)target - (int32_t)cur;

    if (diff >  (int32_t)RM_CHASSIS_ACCEL_STEP)
    {
        return (int16_t)(cur + (int16_t)RM_CHASSIS_ACCEL_STEP);
    }
    if (diff < -(int32_t)RM_CHASSIS_ACCEL_STEP)
    {
        return (int16_t)(cur - (int16_t)RM_CHASSIS_ACCEL_STEP);
    }
    return target;
}

/* 电池补偿系数（千分比）：满电 12.6V，掉到 11.1V 时补 12%，最多补 20% */
static int32_t battery_gain_permille(void)
{
#if (RM_VBAT_COMPENSATE != 0) && (RM_ENABLE_VBAT != 0)
    int32_t mv = (int32_t)s_batt_mv;

    if (mv < 9000)
    {
        return 1000;   /* 没接电压检测或读数无效 */
    }
    if (mv >= (int32_t)RM_VBAT_FULL_MV)
    {
        return 1000;
    }
    /* gain = FULL / mv * 1000，夹在 [1000, 1200] */
    {
        int32_t g = ((int32_t)RM_VBAT_FULL_MV * 1000) / mv;
        if (g > 1200) { g = 1200; }
        if (g < 1000) { g = 1000; }
        return g;
    }
#else
    (void)s_batt_mv;   /* 没开电压检测/补偿时，这个值只记录不使用 */
    return 1000;
#endif
}

/* ==========================================================================
 * 初始化
 * ========================================================================== */
void Chassis_Init(void)
{
    s_vx = 0;
    s_vy = 0;
    s_wz = 0;
    s_cvx = 0;
    s_cvy = 0;
    s_cwz = 0;
    s_batt_mv = 0;
    s_follow = RM_CHASSIS_FOLLOW_DEFAULT;
    s_yaw10  = (int16_t)(RM_GIMBAL_YAW_CENTER_DEG * 10);
    s_test_mode = 0u;
    s_test_idx  = 0u;
    s_test_spd  = 0;
}

/* ==========================================================================
 * 指令
 * ========================================================================== */
void Chassis_SetVelocity(int16_t vx, int16_t vy, int16_t wz)
{
    if (vx >  RM_PWM_MAX) { vx =  RM_PWM_MAX; }
    if (vx < -RM_PWM_MAX) { vx = -RM_PWM_MAX; }
    if (vy >  RM_PWM_MAX) { vy =  RM_PWM_MAX; }
    if (vy < -RM_PWM_MAX) { vy = -RM_PWM_MAX; }
    if (wz >  RM_PWM_MAX) { wz =  RM_PWM_MAX; }
    if (wz < -RM_PWM_MAX) { wz = -RM_PWM_MAX; }

#if (RM_CHASSIS_DEADZONE > 0)
    if ((vx <= (int16_t)RM_CHASSIS_DEADZONE) && (vx >= -(int16_t)RM_CHASSIS_DEADZONE)) { vx = 0; }
    if ((vy <= (int16_t)RM_CHASSIS_DEADZONE) && (vy >= -(int16_t)RM_CHASSIS_DEADZONE)) { vy = 0; }
    if ((wz <= (int16_t)RM_CHASSIS_DEADZONE) && (wz >= -(int16_t)RM_CHASSIS_DEADZONE)) { wz = 0; }
#endif

    s_vx = vx;
    s_vy = vy;
    s_wz = wz;
    s_test_mode = 0u;
}

void Chassis_Stop(void)
{
    s_vx = 0;
    s_vy = 0;
    s_wz = 0;
    s_test_mode = 0u;
    s_test_spd  = 0;
}

void Chassis_SetBatteryMv(int16_t mv)
{
    s_batt_mv = (mv < 0) ? 0 : mv;
}

void Chassis_SetFollow(uint8_t on)
{
    s_follow = (on != 0u) ? 1u : 0u;
}

uint8_t Chassis_GetFollow(void)
{
    return s_follow;
}

void Chassis_SetGimbalYawDeg10(int16_t deg10)
{
    s_yaw10 = deg10;
}

void Chassis_SetTest(uint8_t idx, int16_t speed)
{
    if (Motor_IsValidIdx(idx) == 0u)
    {
        return;
    }
    s_test_mode = 1u;
    s_test_idx  = idx;
    s_test_spd  = speed;
}

void Chassis_ClearTest(void)
{
    s_test_mode = 0u;
    s_test_spd  = 0;
}

uint8_t Chassis_IsTestMode(void)
{
    return s_test_mode;
}

void Chassis_GetVelocity(int16_t *vx, int16_t *vy, int16_t *wz)
{
    if (vx != 0) { *vx = s_cvx; }
    if (vy != 0) { *vy = s_cvy; }
    if (wz != 0) { *wz = s_cwz; }
}

/* ==========================================================================
 * 10ms 任务：斜坡 -> 补偿 -> 逆解 -> 归一化 -> 下发
 * ========================================================================== */
void Chassis_Task(void)
{
    int32_t w[RM_MOTOR_COUNT];
    int32_t m;
    int32_t scale;
    int32_t vx;
    int32_t vy;
    int32_t wz;
    uint8_t i;

    if (s_test_mode != 0u)
    {
        for (i = 0u; i < RM_MOTOR_COUNT; i++)
        {
            Motor_SetTarget(i, (i == s_test_idx) ? s_test_spd : 0);
        }
        return;
    }

    /* 1) 指令斜坡：键盘点一下不会瞬间满占空比 */
    s_cvx = ramp_toward(s_cvx, s_vx);
    s_cvy = ramp_toward(s_cvy, s_vy);
    s_cwz = ramp_toward(s_cwz, s_wz);

    vx = s_cvx;
    vy = s_cvy;
    wz = s_cwz;

    /* 1b) 云台系驱动：把"炮口系"的 vx/vy 旋回车体系
     *     θ = (云台角 - 中位) * 符号；θ=0 时和中位一样，等价于车体系 */
    if (s_follow != 0u)
    {
        int32_t theta10 = ((int32_t)s_yaw10 - (int32_t)RM_GIMBAL_YAW_CENTER_DEG * 10)
                          * (int32_t)RM_CHASSIS_FOLLOW_SIGN;
        float   th  = ((float)theta10 * 0.1f) * 0.0174532925f;
        float   c   = cosf(th);
        float   sn  = sinf(th);
        int32_t rx  = (int32_t)((float)vx * c - (float)vy * sn);
        int32_t ry  = (int32_t)((float)vx * sn + (float)vy * c);

        vx = rx;
        vy = ry;
    }

    /* 2) 装反了整体取反（偷懒开关） */
#if (RM_CHASSIS_INVERT_XY != 0)
    vx = -vx;
    vy = -vy;
#endif

    /* 3) 总速度上限（不含旋转） */
    {
        int32_t mag = (vx >= 0) ? vx : -vx;
        int32_t magy = (vy >= 0) ? vy : -vy;
        int32_t maxm = (mag > magy) ? mag : magy;

        if (maxm > (int32_t)RM_CHASSIS_MAX_SPEED)
        {
            vx = (vx * (int32_t)RM_CHASSIS_MAX_SPEED) / maxm;
            vy = (vy * (int32_t)RM_CHASSIS_MAX_SPEED) / maxm;
        }
    }

    /* 4) 电池补偿 */
    {
        int32_t gain = battery_gain_permille();
        vx = (vx * gain) / 1000;
        vy = (vy * gain) / 1000;
        wz = (wz * gain) / 1000;
    }

    /* 5) 麦轮逆解：wz 乘 k = (轮距/2 + 轴距/2)，单位 mm -> 千分比尺度 */
    {
        int32_t k = (int32_t)(RM_WHEEL_BASE_MM + RM_TRACK_MM) / 2;
        w[RM_WHEEL_FL] = vx - vy - (wz * k) / 1000;
        w[RM_WHEEL_RL] = vx + vy - (wz * k) / 1000;
        w[RM_WHEEL_RR] = vx + vy + (wz * k) / 1000;
        w[RM_WHEEL_FR] = vx - vy + (wz * k) / 1000;
    }

    /* 6) 归一化到电机限幅，保证比例不失真 */
    m = 0;
    for (i = 0u; i < RM_MOTOR_COUNT; i++)
    {
        int32_t a = (w[i] >= 0) ? w[i] : -w[i];
        if (a > m)
        {
            m = a;
        }
    }

    if (m > (int32_t)RM_MOTOR_LIMIT)
    {
        scale = ((int32_t)RM_MOTOR_LIMIT * 1000) / m;
        for (i = 0u; i < RM_MOTOR_COUNT; i++)
        {
            w[i] = (w[i] * scale) / 1000;
        }
    }

    for (i = 0u; i < RM_MOTOR_COUNT; i++)
    {
        Motor_SetTarget(i, (int16_t)w[i]);
    }
}
