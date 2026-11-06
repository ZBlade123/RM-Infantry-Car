/**
  ******************************************************************************
  * @file    rm_app.h
  * @brief   应用层：初始化顺序、1ms/10ms/100ms 调度、安全监管、指令分发
  ******************************************************************************
  */

#ifndef __RM_APP_H
#define __RM_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void RM_App_Init(void);
void RM_App_Loop(void);

#if (RM_ENABLE_REFEREE != 0)
/**
  * @brief 把裁判系统串口收到的原始字节喂进来（在 UART 接收中断/DMA 回调里调用）
  * @note  只有 RM_ENABLE_REFEREE=1 时才编译。参考用法：
  *          void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  *              if (huart->Instance == USART1) {  // 裁判系统那一路
  *                  RM_App_RefereeRx(g_ref_rx, 1);
  *                  HAL_UART_Receive_IT(huart, g_ref_rx, 1);
  *              }
  *          }
  */
void RM_App_RefereeRx(const uint8_t *data, uint16_t len);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __RM_APP_H */
