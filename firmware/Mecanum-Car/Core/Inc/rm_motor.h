/**
  ******************************************************************************
  * @file    rm_motor.h
  * @brief   电机驱动层（BSP/Driver 层）：4 路 TIM2 PWM + 8 个方向脚
  *          对外只暴露"逻辑速度"概念：+1000 = 该轮朝车头方向转
  ******************************************************************************
  */

#ifndef __RM_MOTOR_H
#define __RM_MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 轮序（固定，不要改顺序，物理对应关系在 rm_motor.c 的表里） */
typedef enum
{
    RM_WHEEL_FL = 0,        /* 左前 */
    RM_WHEEL_RL = 1,        /* 左后 */
    RM_WHEEL_RR = 2,        /* 右后 */
    RM_WHEEL_FR = 3         /* 右前 */
} rm_wheel_t;

void    Motor_Init(void);
void    Motor_SetTarget(uint8_t idx, int16_t logical);   /* -1000 ~ +1000 */
void    Motor_Task(void);                                /* 10ms 调用：斜坡 + 输出 */
void    Motor_StopAll(void);                             /* 目标清零（走斜坡） */
void    Motor_EmergencyStop(void);                       /* 立即断电（斜坡清零） */
int16_t Motor_GetTarget(uint8_t idx);
int16_t Motor_GetOutput(uint8_t idx);
int8_t  Motor_GetSign(uint8_t idx);
int16_t Motor_GetTrim(uint8_t idx);
void    Motor_FlipSign(uint8_t idx);
void    Motor_SetTrim(uint8_t idx, int16_t trim);
uint8_t Motor_IsValidIdx(uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif /* __RM_MOTOR_H */
