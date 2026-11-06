/**
  ******************************************************************************
  * @file    rm_pid.c
  * @brief   PID 控制器实现
  *
  * 实现要点（都是从参考工程踩坑总结的）：
  *   1) 积分项单独限幅，且限幅值要和输出量纲配套（见 rm_pid.h 注释）；
  *   2) 输出饱和时撤销本次积分（条件积分抗饱和），避免"饱和后积分爆掉、
  *      反向指令要等很久才回得来"；
  *   3) 微分项可选"对测量值差分"，目标值阶跃时不会打出冲击；
  *   4) 角度环把误差折叠到 ±180°，避免 179° 到 -179° 这种边界绕远路。
  ******************************************************************************
  */

#include "rm_pid.h"

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

static float wrap180(float deg)
{
    while (deg > 180.0f)
    {
        deg -= 360.0f;
    }
    while (deg <= -180.0f)
    {
        deg += 360.0f;
    }
    return deg;
}

void PID_Init(rm_pid_t *pid, const rm_pid_cfg_t *cfg)
{
    if (pid == 0)
    {
        return;
    }

    if (cfg != 0)
    {
        pid->cfg = *cfg;
    }
    PID_Reset(pid);
}

void PID_Reset(rm_pid_t *pid)
{
    if (pid == 0)
    {
        return;
    }

    pid->set      = 0.0f;
    pid->get      = 0.0f;
    pid->err      = 0.0f;
    pid->last_err = 0.0f;
    pid->last_get = 0.0f;
    pid->p_out    = 0.0f;
    pid->i_out    = 0.0f;
    pid->d_out    = 0.0f;
    pid->d_state  = 0.0f;
    pid->out      = 0.0f;
}

void PID_SetTarget(rm_pid_t *pid, float set)
{
    if (pid == 0)
    {
        return;
    }
    pid->set = set;
}

float PID_Step(rm_pid_t *pid, float get, float ff)
{
    float err;
    float d_raw;
    float raw;
    float out;

    if (pid == 0)
    {
        return 0.0f;
    }

    /* 1) 误差 */
    err = pid->set - get;
    if (pid->cfg.angle_wrap != 0u)
    {
        err = wrap180(err);
    }

    /* 2) 比例项 */
    pid->p_out = pid->cfg.kp * err;

    /* 3) 微分项：默认对误差差分，开启 d_on_measurement 后对测量值差分 */
    if (pid->cfg.d_on_measurement != 0u)
    {
        d_raw = -(get - pid->last_get);
    }
    else
    {
        d_raw = err - pid->last_err;
    }
    if (pid->cfg.d_lpf > 0.0f)
    {
        pid->d_state += pid->cfg.d_lpf * (d_raw - pid->d_state);
        d_raw = pid->d_state;
    }
    pid->d_out = pid->cfg.kd * d_raw;

    /* 4) 积分项 + 条件抗饱和 */
    pid->i_out += pid->cfg.ki * err;
    pid->i_out  = clampf(pid->i_out, pid->cfg.i_min, pid->cfg.i_max);

    raw = pid->p_out + pid->i_out + pid->d_out + ff;
    out = clampf(raw, pid->cfg.out_min, pid->cfg.out_max);

    if ((out != raw) && ((pid->cfg.ki * err) != 0.0f))
    {
        /* 输出被限幅：撤销本次积分，防止积分继续往饱和方向堆 */
        pid->i_out -= pid->cfg.ki * err;
        pid->i_out  = clampf(pid->i_out, pid->cfg.i_min, pid->cfg.i_max);
        raw = pid->p_out + pid->i_out + pid->d_out + ff;
        out = clampf(raw, pid->cfg.out_min, pid->cfg.out_max);
    }

    pid->err      = err;
    pid->get      = get;
    pid->last_err = err;
    pid->last_get = get;
    pid->out      = out;

    return out;
}

float PID_Calc(rm_pid_t *pid, float set, float get)
{
    if (pid == 0)
    {
        return 0.0f;
    }
    pid->set = set;
    return PID_Step(pid, get, 0.0f);
}
