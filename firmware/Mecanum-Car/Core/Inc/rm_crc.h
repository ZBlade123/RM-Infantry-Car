/**
  ******************************************************************************
  * @file    rm_crc.h
  * @brief   CRC 工具（算法层，纯函数，不依赖 HAL）
  *
  * 来源/参考：2026_new_sentry-main/stm32_ws/Devices/Src/referee_protocol.c
  *   （对照官方裁判系统协议手册）
  *
  * 说明：
  *   裁判系统用两种 CRC：
  *     帧头 CRC8  : 多项式 0x31，初值 0xFF，只算前 4 字节(A5 len_lo len_hi seq)
  *     数据 CRC16 : 多项式 0x1021，初值 0xFFFF，算 cmd_id + data
  *   参考工程用的是 256 项查表（768 字节 flash）。这里改成逐位计算：
  *     115200bps 下每帧最多 192 字节，逐位算也只有几十微秒，
  *     省下 768 字节 flash 更划算（F103C8T6 只有 64KB）。
  ******************************************************************************
  */

#ifndef __RM_CRC_H
#define __RM_CRC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 裁判系统 CRC8：多项式 0x31，初值 0xFF */
uint8_t  RM_Crc8(uint8_t init, const uint8_t *data, uint16_t len);

/* 裁判系统 CRC16：多项式 0x1021（CCITT），初值 0xFFFF */
uint16_t RM_Crc16(uint16_t init, const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __RM_CRC_H */
