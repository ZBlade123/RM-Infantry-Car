/**
  ******************************************************************************
  * @file    rm_remote.h
  * @brief   串口链路层：环形缓冲 + 帧解析 + 文本指令 + 遥测发送
  *          上下文：蓝牙 HC-05 / USB-TTL 调试线，共用 USART1 (PA9/PA10)
  ******************************************************************************
  */

#ifndef __RM_REMOTE_H
#define __RM_REMOTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "rm_protocol.h"

void     Remote_Init(void);
void     Remote_Task(void);                    /* 1ms 调用：取空环形缓冲并解析 */

/* 注册指令回调（内部会同时初始化协议解析器） */
void     Remote_SetHandler(rm_cmd_handler_t handler);

uint8_t  Remote_LinkOk(void);
uint32_t Remote_GetLastRxMs(void);
void     Remote_LinkTimerReset(void);          /* 收到有效指令时刷新 */
void     Remote_SetKeepAlive(uint8_t on);
uint8_t  Remote_GetKeepAlive(void);

/* 发送 */
void     Remote_TxFrame(uint8_t cmd, const uint8_t *payload, uint8_t len);
void     Remote_TxText(const char *s);         /* 阻塞发送，只给人工指令回显用 */

uint16_t Remote_GetFrameCount(void);
uint16_t Remote_GetTextCount(void);
uint16_t Remote_GetErrorCount(void);

#ifdef __cplusplus
}
#endif

#endif /* __RM_REMOTE_H */
