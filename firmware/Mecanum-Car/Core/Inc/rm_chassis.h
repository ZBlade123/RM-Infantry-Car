/**
  ******************************************************************************
  * @file    rm_chassis.h
  * @brief   底盘解算层（Algorithm 层）：麦轮逆运动学 + 单轮标定/点动
  *          输入是"车体速度指令"，输出是四个轮子的逻辑速度
  ******************************************************************************
  */

#ifndef __RM_CHASSIS_H
#define __RM_CHASSIS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void Chassis_Init(void);

/* vx: 前后(+前进) vy: 左右(+左移) wz: 自转(+逆时针)，范围 -1000 ~ +1000 */
void Chassis_SetVelocity(int16_t vx, int16_t vy, int16_t wz);
void Chassis_Stop(void);
void Chassis_Task(void);                       /* 10ms 调用 */

/* 台架标定：只让某一个轮子转，其余停 */
void    Chassis_SetTest(uint8_t idx, int16_t speed);
void    Chassis_ClearTest(void);
uint8_t Chassis_IsTestMode(void);

/* 电池电压（mV）：给"电压补偿"用；0 或 <9000 表示未知，不补偿 */
void Chassis_SetBatteryMv(int16_t mv);

/* 云台系驱动：on=1 时 vx/vy 按"炮口方向"解释（需每 10ms 喂一次云台角度） */
void    Chassis_SetFollow(uint8_t on);
uint8_t Chassis_GetFollow(void);
void    Chassis_SetGimbalYawDeg10(int16_t deg10);

void Chassis_GetVelocity(int16_t *vx, int16_t *vy, int16_t *wz);

#ifdef __cplusplus
}
#endif

#endif /* __RM_CHASSIS_H */
