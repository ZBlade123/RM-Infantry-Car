/**
  ******************************************************************************
  * @file    rm_servo.h
  * @brief   二维云台（Driver 层）：Yaw + Pitch
  *          - PWM 模式：Yaw=PA8/TIM1_CH1，Pitch=PB0/TIM3_CH3，50Hz
  *          - 总线模式：USART3(PB10) 115200，帧 #idPpwmTtime!
  *          对外只有"目标角度"，内部自己限位、平滑、上电回中
  ******************************************************************************
  */

#ifndef __RM_SERVO_H
#define __RM_SERVO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void    Servo_Init(void);
/* 总线模式专用：原样发一串维护指令（改 ID / 设偏差），PWM 模式是空函数 */
void    Gimbal_BusRaw(const char *s);
void    Servo_Task(void);                        /* 10ms 调用 */

/* ---------- 俯仰 Pitch（板载 PB0，旧接口保持不变） ---------- */
void    Servo_SetTargetDeg10(int16_t deg_x10);   /* 750 = 75.0° */
void    Servo_SetTargetDeg(int16_t deg);
int16_t Servo_GetDeg10(void);
int16_t Servo_GetTargetDeg10(void);
uint16_t Servo_GetPulseUs(void);

/* ---------- 偏航 Yaw（PA8 飞线） ---------- */
void    Yaw_SetTargetDeg10(int16_t deg_x10);
int16_t Yaw_GetDeg10(void);
int16_t Yaw_GetTargetDeg10(void);
uint16_t Yaw_GetPulseUs(void);

/* ---------- 两轴一起 ---------- */
void    Gimbal_SetDeg10(int16_t yaw_x10, int16_t pitch_x10);
uint8_t Servo_IsHoming(void);                    /* 1 = 还在上电回中，忽略指令 */

#ifdef __cplusplus
}
#endif

#endif /* __RM_SERVO_H */
