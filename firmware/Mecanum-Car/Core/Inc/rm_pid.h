/**
  ******************************************************************************
  * @file    rm_pid.h
  * @brief   PID 控制器（算法层，纯浮点，不依赖 HAL）
  *
  * 参考：
  *   1) 2026_new_sentry-main/stm32_ws/Algorithm/Src/pid_core.[ch]
  *      位置式差分方程 + 积分/输出限幅 + 误差 hook（角度环包一层 wrap）
  *   2) 2026_robot_base-main/.../Middware/App/app_chassis_board.h
  *      编码器速度环的实战教训：积分上限要和输出上限配套
  *      （实测 MAX_IOUT 必须约等于 PWM_MAX/Ki，否则低速段"积分=关掉"）
  *
  * 本工程当前是开环（无编码器/无 IMU），PID 先不接控制环，
  * 等装上编码器（轮速环）或 IMU（云台 yaw 环）再直接复用本模块。
  * 正确性用 tools/hosttest/pid_selftest.c 在 PC 上验证。
  ******************************************************************************
  */

#ifndef __RM_PID_H
#define __RM_PID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 配置：参数 + 限幅 + 策略开关 */
typedef struct
{
    float   kp;
    float   ki;
    float   kd;
    float   out_min;        /* 输出下限 */
    float   out_max;        /* 输出上限 */
    float   i_min;          /* 积分项下限（抗饱和） */
    float   i_max;          /* 积分项上限 */
    float   d_lpf;          /* 微分一阶滤波系数 0~1，0 表示不滤波 */
    uint8_t angle_wrap;     /* 1 = 误差折叠到 (-180, +180]，用于角度环 */
    uint8_t d_on_measurement; /* 1 = 微分项用测量值差分（避免目标跳变冲击） */
} rm_pid_cfg_t;

/* 运行态 */
typedef struct
{
    rm_pid_cfg_t cfg;
    float set;
    float get;
    float err;
    float last_err;
    float last_get;
    float p_out;
    float i_out;
    float d_out;
    float d_state;          /* 微分项滤波状态 */
    float out;
} rm_pid_t;

void  PID_Init(rm_pid_t *pid, const rm_pid_cfg_t *cfg);
void  PID_Reset(rm_pid_t *pid);
void  PID_SetTarget(rm_pid_t *pid, float set);

/* 单步计算：get = 反馈值，ff = 前馈（可直接写 0） */
float PID_Step(rm_pid_t *pid, float get, float ff);

/* 便捷封装：PID_Step(set, get, 0) */
float PID_Calc(rm_pid_t *pid, float set, float get);

#ifdef __cplusplus
}
#endif

#endif /* __RM_PID_H */
