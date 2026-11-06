#include "bluetooth.h"
#include "usart.h"
#include "car.h"
#include "servo.h"


/* =========================================================
 * ??????
 * ========================================================= */

static uint8_t bluetooth_rx_data;
uint32_t bluetooth_last_time = 0;


/* =========================================================
 * ?????
 * ========================================================= */

void Bluetooth_Init(void)
{
    /*
     * USART1????1???
     *
     * PA9  = USART1_TX
     * PA10 = USART1_RX
     */
		bluetooth_last_time = HAL_GetTick();
    HAL_UART_Receive_IT(&huart1,
                        &bluetooth_rx_data,
                        1);
}


/* =========================================================
 * ??????????
 * ========================================================= */

uint8_t Bluetooth_GetCommand(void)
{
    return bluetooth_rx_data;
}


/* =========================================================
 * USART1??????
 *
 * ???1???,??????
 * ========================================================= */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {


        bluetooth_last_time =
            HAL_GetTick();


        HAL_UART_Receive_IT(
            &huart1,
            &bluetooth_rx_data,
            1
        );

    }
}


/* =========================================================
 * ??????
 * ========================================================= */

void Bluetooth_Task(void)
{


    // ===============================
    // ??????
    // ===============================

    if(
        HAL_GetTick()
        -
        bluetooth_last_time
        >
        500
    )
    {

        Car_Stop();

        return;

    }



    static uint8_t last_command = 0;

    uint8_t command;


    command = bluetooth_rx_data;


    /*
     * ?????
     */
    if (command == last_command)
    {
        return;
    }


    /*
     * ??????
     */
    last_command = command;


    /* =====================================================
     * ????
     * ===================================================== */

    switch (command)
    {

        /* ?? */
        case 'W':
        case 'w':

            Car_Forward(400);

            break;


        /* ?? */
        case 'S':
        case 's':

            Car_Backward(400);

            break;


        /* ?? */
        case 'A':
        case 'a':

            Car_Left(400);

            break;


        /* ?? */
        case 'D':
        case 'd':

            Car_Right(400);

            break;


        /* ?? */
        case 'Q':
        case 'q':

            Car_ForwardLeft(400);

            break;


        /* ?? */
        case 'E':
        case 'e':

            Car_ForwardRight(400);

            break;


        /* ?? */
        case 'Z':
        case 'z':

            Car_BackwardLeft(400);

            break;


        /* ?? */
        case 'C':
        case 'c':

            Car_BackwardRight(400);

            break;


        /* ??? */
        case 'J':
        case 'j':

            Car_RotateLeft(400);

            break;


        /* ??? */
        case 'K':
        case 'k':

            Car_RotateRight(400);

            break;


        /* ?? */
        case 'X':
        case 'x':

            Car_Stop();

            break;


        /* =================================================
         * ??
         * ================================================= */

        /* 30° */
        case '1':

            Servo_Set30();

            break;


        /* 45° */
        case '2':

            Servo_Set45();

            break;


        /* 60° */
        case '3':

            Servo_Set60();

            break;


        /* =================================================
         * ??
         * ================================================= */

        /* ?? */
        case 'F':
        case 'f':

            HAL_GPIO_WritePin(GPIOB,
                              GPIO_PIN_14,
                              GPIO_PIN_SET);

            break;


        /* ???? */
        case 'R':
        case 'r':

            HAL_GPIO_WritePin(GPIOB,
                              GPIO_PIN_14,
                              GPIO_PIN_RESET);

            break;


        default:

            break;
    }
}