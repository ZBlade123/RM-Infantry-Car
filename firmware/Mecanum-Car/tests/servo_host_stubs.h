#ifndef SERVO_HOST_STUBS_H
#define SERVO_HOST_STUBS_H
#include <stdint.h>
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
typedef struct {
    uint32_t BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl, OverSampling;
} UART_InitTypeDef;
typedef struct { void *Instance; UART_InitTypeDef Init; } UART_HandleTypeDef;
#define GPIOB ((void *)1)
#define USART3 ((void *)3)
#define GPIO_PIN_10 1024u
#define GPIO_PIN_14 16384u
#define GPIO_PIN_SET 1u
#define GPIO_PIN_RESET 0u
#define GPIO_MODE_AF_OD 1u
#define GPIO_NOPULL 0u
#define GPIO_SPEED_FREQ_HIGH 3u
#define UART_WORDLENGTH_8B 8u
#define UART_STOPBITS_1 1u
#define UART_PARITY_NONE 0u
#define UART_MODE_TX 1u
#define UART_HWCONTROL_NONE 0u
#define UART_OVERSAMPLING_16 16u
#define __HAL_RCC_USART3_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
uint32_t HAL_GetTick(void);
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio);
void HAL_GPIO_WritePin(void *port, uint32_t pin, uint32_t state);
int HAL_UART_Init(UART_HandleTypeDef *uart);
int HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data, uint16_t len, uint32_t timeout);
#endif
