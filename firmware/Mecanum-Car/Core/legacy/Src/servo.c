#include "servo.h"

extern TIM_HandleTypeDef htim3;


/* ????? */
void Servo_Init(void)
{
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);

    /* ????45° */
    Servo_Set45();
}


/* ?????? */
void Servo_SetAngle(uint8_t angle)
{
    uint16_t pulse;

    if (angle > 180)
    {
        angle = 180;
    }

    /*
     * 500us ~ 2500us
     *
     * 0°   = 500
     * 90°  = 1500
     * 180° = 2500
     */

    pulse = 500 + ((uint32_t)angle * 2000) / 180;

    __HAL_TIM_SET_COMPARE(&htim3,
                          TIM_CHANNEL_3,
                          pulse);
}


/* 30° */
void Servo_Set30(void)
{
    Servo_SetAngle(SERVO_ANGLE_30);
}


/* 45° */
void Servo_Set45(void)
{
    Servo_SetAngle(SERVO_ANGLE_45);
}


/* 60° */
void Servo_Set60(void)
{
    Servo_SetAngle(SERVO_ANGLE_60);
}