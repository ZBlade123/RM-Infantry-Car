#ifndef __SERVO_H
#define __SERVO_H

#include "main.h"

#define SERVO_ANGLE_30    30
#define SERVO_ANGLE_45    45
#define SERVO_ANGLE_60    60

void Servo_Init(void);
void Servo_SetAngle(uint8_t angle);

void Servo_Set30(void);
void Servo_Set45(void);
void Servo_Set60(void);

#endif