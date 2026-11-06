/**
  ******************************************************************************
  * @file    rm_protocol.h
  * @brief   通信帧编解码（纯逻辑，不依赖硬件）
  *          帧格式（小端）：
  *            | 0xA5 | 0x5A | seq | cmd | len | payload[len] | crc8 |
  *          crc8 覆盖 seq/cmd/len/payload（不含帧头），多项式 0x07，初值 0
  ******************************************************************************
  */

#ifndef __RM_PROTOCOL_H
#define __RM_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define RM_FRAME_H0               0xA5u
#define RM_FRAME_H1               0x5Au
#define RM_FRAME_OVERHEAD         6u    /* 2 帧头 + seq + cmd + len + crc */
#define RM_FRAME_MAX_PAYLOAD      32u

/* 上位机 -> 下位机 */
#define RM_CMD_VEL                0x01u  /* int16 vx, int16 vy, int16 wz  (-1000~1000) */
#define RM_CMD_SERVO              0x02u  /* int16 角度 x10 */
#define RM_CMD_FIRE               0x03u  /* uint8 mode(0停/1单发/2连发), uint8 保留 */
#define RM_CMD_PING               0x04u  /* 无 payload */
#define RM_CMD_WHEELTEST          0x05u  /* uint8 轮序, int16 速度 */
#define RM_CMD_FLIP               0x06u  /* uint8 轮序，翻转方向标定 */
#define RM_CMD_TELEM              0x07u  /* uint8 0/1 开关遥测 */
#define RM_CMD_STOP               0x08u  /* 全部停车 */
#define RM_CMD_CAL                0x09u  /* 无 payload，回显当前标定 */
#define RM_CMD_REF                0x0Au  /* uint8 'r'=查询裁判状态 't'=内置自检 */
#define RM_CMD_KBM                0x0Bu  /* 无 payload，回显键鼠状态（调试用） */

/* 下位机 -> 上位机 */
#define RM_RSP_TELEM              0x81u
#define RM_RSP_PONG               0x82u
#define RM_RSP_ACK                0x83u
#define RM_RSP_TEXT               0x84u
#define RM_RSP_REF                0x85u  /* 裁判系统遥测（需 RM_ENABLE_REFEREE=1） */

typedef void (*rm_cmd_handler_t)(uint8_t cmd, const uint8_t *payload, uint8_t len);

void    Proto_Init(rm_cmd_handler_t handler);
void    Proto_Reset(void);
/* 返回 1 = 这个字节被帧状态机消费；返回 0 = 当前不在帧内，字节可当文本处理。
 * 串口是"帧 + 人眼可读文本"混跑的，靠这个返回值分流，互不干扰。 */
uint8_t Proto_ParseByte(uint8_t byte, uint32_t now_ms);
uint8_t Proto_Crc8(const uint8_t *data, uint8_t len);
uint8_t Proto_BuildFrame(uint8_t *out, uint8_t cmd, const uint8_t *payload, uint8_t len);

/* 小工具：小端读写 */
int16_t  Proto_GetI16(const uint8_t *p);
uint16_t Proto_GetU16(const uint8_t *p);
void     Proto_PutI16(uint8_t *p, int16_t v);
void     Proto_PutU16(uint8_t *p, uint16_t v);
void     Proto_PutU32(uint8_t *p, uint32_t v);

#ifdef __cplusplus
}
#endif

#endif /* __RM_PROTOCOL_H */

