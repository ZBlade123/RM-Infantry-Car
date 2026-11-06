#ifndef __MOTOR_H
#define __MOTOR_H

#include "main.h"

typedef enum
{
    MOTOR_1 = 1,
    MOTOR_2,
    MOTOR_3,
    MOTOR_4
} Motor_ID;

void Motor_Init(void);

void Motor_SetSpeed(Motor_ID motor, int16_t speed);

void Motor_Stop(Motor_ID motor);

void Motor_StopAll(void);

#endif