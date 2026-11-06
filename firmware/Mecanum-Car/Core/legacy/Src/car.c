#include "car.h"
#include "motor.h"


/*
 * ============================================================
 * ????????
 *
 *                  ??
 *
 *          M1              M4
 *         ??              ??
 *
 *
 *          M2              M3
 *         ??              ??
 *
 *                  ??
 *
 *
 * ??????:
 *
 * M1:+ = ??
 * M2:+ = ??
 * M3:+ = ??
 * M4:+ = ??
 *
 * ============================================================
 */


/* ============================================================
 * ??
 * ============================================================ */
void Car_Forward(int16_t speed)
{
    Motor_SetSpeed(MOTOR_1,  speed);
    Motor_SetSpeed(MOTOR_2,  speed);
    Motor_SetSpeed(MOTOR_3, -speed);
    Motor_SetSpeed(MOTOR_4, -speed);
}


/* ============================================================
 * ??
 * ============================================================ */
void Car_Backward(int16_t speed)
{
    Motor_SetSpeed(MOTOR_1, -speed);
    Motor_SetSpeed(MOTOR_2, -speed);
    Motor_SetSpeed(MOTOR_3,  speed);
    Motor_SetSpeed(MOTOR_4,  speed);
}


/* ============================================================
 * ??
 * ============================================================ */
void Car_Left(int16_t speed)
{
    Motor_SetSpeed(MOTOR_1, -speed);
    Motor_SetSpeed(MOTOR_2,  speed);
    Motor_SetSpeed(MOTOR_3,  speed);
    Motor_SetSpeed(MOTOR_4, -speed);
}


/* ============================================================
 * ??
 * ============================================================ */
void Car_Right(int16_t speed)
{
    Motor_SetSpeed(MOTOR_1,  speed);
    Motor_SetSpeed(MOTOR_2, -speed);
    Motor_SetSpeed(MOTOR_3, -speed);
    Motor_SetSpeed(MOTOR_4,  speed);
}


/* ============================================================
 * ???
 * ============================================================ */
void Car_ForwardLeft(int16_t speed)
{
    Motor_SetSpeed(MOTOR_1, 0);
    Motor_SetSpeed(MOTOR_2, speed);
    Motor_SetSpeed(MOTOR_3, 0);
    Motor_SetSpeed(MOTOR_4, -speed);
}

/* ============================================================
 * ???
 * ============================================================ */
void Car_ForwardRight(int16_t speed)
{
    Motor_SetSpeed(MOTOR_1, speed);
    Motor_SetSpeed(MOTOR_2, 0);
    Motor_SetSpeed(MOTOR_3, -speed);
    Motor_SetSpeed(MOTOR_4, 0);
}

/* ============================================================
 * ???
 * ============================================================ */
void Car_BackwardLeft(int16_t speed)
{
    Motor_SetSpeed(MOTOR_1, -speed);
    Motor_SetSpeed(MOTOR_2, 0);
    Motor_SetSpeed(MOTOR_3, speed);
    Motor_SetSpeed(MOTOR_4, 0);
}

/* ============================================================
 * ???
 * ============================================================ */
void Car_BackwardRight(int16_t speed)
{
    Motor_SetSpeed(MOTOR_1, 0);
    Motor_SetSpeed(MOTOR_2, -speed);
    Motor_SetSpeed(MOTOR_3, 0);
    Motor_SetSpeed(MOTOR_4, speed);
}

/* ============================================================
 * ?????
 * ============================================================ */
void Car_RotateLeft(int16_t speed)
{
    Motor_SetSpeed(MOTOR_1, -speed);
    Motor_SetSpeed(MOTOR_2, -speed);
    Motor_SetSpeed(MOTOR_3, -speed);
    Motor_SetSpeed(MOTOR_4, -speed);
}


/* ============================================================
 * ?????
 * ============================================================ */
void Car_RotateRight(int16_t speed)
{
    Motor_SetSpeed(MOTOR_1, speed);
    Motor_SetSpeed(MOTOR_2, speed);
    Motor_SetSpeed(MOTOR_3, speed);
    Motor_SetSpeed(MOTOR_4, speed);
}


/* ============================================================
 * ??
 * ============================================================ */
void Car_Stop(void)
{
    Motor_StopAll();
}