#include "motor.h"

extern TIM_HandleTypeDef htim2;


/*
 * speed??:
 *
 * -1000 ~ +1000
 *
 *  0      = ??
 *  ??   = ???
 *  ??   = ???
 */


/* ==============================
   ?????
   ============================== */
void Motor_Init(void)
{
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);

    Motor_StopAll();
}


/* ==============================
   ????????
   ============================== */
void Motor_SetSpeed(Motor_ID motor, int16_t speed)
{
    uint16_t pwm;

    /* ?? */
    if (speed > 1000)
        speed = 1000;

    if (speed < -1000)
        speed = -1000;


    /* ???? */
    if (speed >= 0)
        pwm = speed;
    else
        pwm = -speed;


    switch (motor)
    {
        /* ==========================
           MOTOR 1

           PA1 ? TIM2_CH2 ? PWM

           PA4 ? AIN1
           PA5 ? AIN2
           ========================== */
        case MOTOR_1:

            if (speed > 0)
            {
                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_4,
                                  GPIO_PIN_SET);

                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_5,
                                  GPIO_PIN_RESET);
            }
            else if (speed < 0)
            {
                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_4,
                                  GPIO_PIN_RESET);

                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_5,
                                  GPIO_PIN_SET);
            }
            else
            {
                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_4,
                                  GPIO_PIN_RESET);

                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_5,
                                  GPIO_PIN_RESET);
            }

            __HAL_TIM_SET_COMPARE(&htim2,
                                  TIM_CHANNEL_2,
                                  pwm);

            break;


        /* ==========================
           MOTOR 2

           PA2 ? TIM2_CH3 ? PWM

           PA6 ? BIN1
           PA7 ? BIN2
           ========================== */
        case MOTOR_2:

            if (speed > 0)
            {
                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_6,
                                  GPIO_PIN_SET);

                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_7,
                                  GPIO_PIN_RESET);
            }
            else if (speed < 0)
            {
                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_6,
                                  GPIO_PIN_RESET);

                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_7,
                                  GPIO_PIN_SET);
            }
            else
            {
                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_6,
                                  GPIO_PIN_RESET);

                HAL_GPIO_WritePin(GPIOA,
                                  GPIO_PIN_7,
                                  GPIO_PIN_RESET);
            }

            __HAL_TIM_SET_COMPARE(&htim2,
                                  TIM_CHANNEL_3,
                                  pwm);

            break;


        /* ==========================
           MOTOR 3

           PA0 ? TIM2_CH1 ? PWM

           PB10 ? AIN1
           PB11 ? AIN2
           ========================== */
        case MOTOR_3:

            if (speed > 0)
            {
                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_10,
                                  GPIO_PIN_SET);

                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_11,
                                  GPIO_PIN_RESET);
            }
            else if (speed < 0)
            {
                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_10,
                                  GPIO_PIN_RESET);

                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_11,
                                  GPIO_PIN_SET);
            }
            else
            {
                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_10,
                                  GPIO_PIN_RESET);

                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_11,
                                  GPIO_PIN_RESET);
            }

            __HAL_TIM_SET_COMPARE(&htim2,
                                  TIM_CHANNEL_1,
                                  pwm);

            break;


        /* ==========================
           MOTOR 4

           PA3 ? TIM2_CH4 ? PWM

           PB12 ? BIN1
           PB13 ? BIN2
           ========================== */
        case MOTOR_4:

            if (speed > 0)
            {
                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_12,
                                  GPIO_PIN_SET);

                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_13,
                                  GPIO_PIN_RESET);
            }
            else if (speed < 0)
            {
                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_12,
                                  GPIO_PIN_RESET);

                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_13,
                                  GPIO_PIN_SET);
            }
            else
            {
                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_12,
                                  GPIO_PIN_RESET);

                HAL_GPIO_WritePin(GPIOB,
                                  GPIO_PIN_13,
                                  GPIO_PIN_RESET);
            }

            __HAL_TIM_SET_COMPARE(&htim2,
                                  TIM_CHANNEL_4,
                                  pwm);

            break;


        default:
            break;
    }
}


/* ==============================
   ??????
   ============================== */
void Motor_Stop(Motor_ID motor)
{
    switch (motor)
    {
        case MOTOR_1:
            __HAL_TIM_SET_COMPARE(&htim2,
                                  TIM_CHANNEL_2,
                                  0);
            break;

        case MOTOR_2:
            __HAL_TIM_SET_COMPARE(&htim2,
                                  TIM_CHANNEL_3,
                                  0);
            break;

        case MOTOR_3:
            __HAL_TIM_SET_COMPARE(&htim2,
                                  TIM_CHANNEL_1,
                                  0);
            break;

        case MOTOR_4:
            __HAL_TIM_SET_COMPARE(&htim2,
                                  TIM_CHANNEL_4,
                                  0);
            break;

        default:
            break;
    }
}


/* ==============================
   ??????
   ============================== */
void Motor_StopAll(void)
{
    __HAL_TIM_SET_COMPARE(&htim2,
                          TIM_CHANNEL_1,
                          0);

    __HAL_TIM_SET_COMPARE(&htim2,
                          TIM_CHANNEL_2,
                          0);

    __HAL_TIM_SET_COMPARE(&htim2,
                          TIM_CHANNEL_3,
                          0);

    __HAL_TIM_SET_COMPARE(&htim2,
                          TIM_CHANNEL_4,
                          0);
}