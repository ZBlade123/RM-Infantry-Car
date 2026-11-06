/**
  ******************************************************************************
  * @file    rm_shooter.h
  * @brief   发射机构（Module 层）：波箱电机 + 有限状态机
  *          模式：0 停 / 1 单发(一个活塞循环) / 2 连发 / 3 三连发；另有反转清卡弹
  *          单发与三连发结束后有 RM_FIRE_COOLDOWN_MS 冷却；连发不限冷却但有时长上限
  ******************************************************************************
  */

#ifndef __RM_SHOOTER_H
#define __RM_SHOOTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum
{
    RM_FIRE_IDLE = 0,
    RM_FIRE_FIRING,
    RM_FIRE_REVERSE
} rm_fire_state_t;

void    Shooter_Init(void);
void    Shooter_SetMode(uint8_t mode);   /* 0 停 / 1 单发 / 2 连发 / 3 三连发 */
void    Shooter_Reverse(void);           /* 反转清卡弹（需要 IN2 已接） */
void    Shooter_Stop(void);
void    Shooter_Task(void);              /* 10ms：状态机 / 软启动斜坡 */
void    Shooter_PwmTick(void);           /* 1ms：软件 PWM 输出 */

uint8_t Shooter_GetMode(void);
uint8_t Shooter_GetState(void);
uint8_t Shooter_GetDuty(void);

#ifdef __cplusplus
}
#endif

#endif /* __RM_SHOOTER_H */
