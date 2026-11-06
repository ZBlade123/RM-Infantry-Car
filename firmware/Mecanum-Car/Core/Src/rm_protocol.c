/**
  ******************************************************************************
  * @file    rm_protocol.c
  * @brief   通信帧编解码
  ******************************************************************************
  */

#include "rm_protocol.h"

/* ==========================================================================
 * 内部状态
 * ========================================================================== */
typedef enum
{
    PS_H0 = 0,
    PS_H1,
    PS_SEQ,
    PS_CMD,
    PS_LEN,
    PS_PAYLOAD,
    PS_CRC
} proto_state_t;

static proto_state_t    s_state;
static rm_cmd_handler_t s_handler;
static uint8_t          s_tx_seq;
static uint8_t          s_cmd;
static uint8_t          s_len;
static uint8_t          s_idx;
static uint8_t          s_crc;
static uint8_t          s_buf[RM_FRAME_MAX_PAYLOAD];
static uint32_t         s_last_ms;

/* ==========================================================================
 * CRC8 (poly 0x07, init 0x00)，支持增量累加
 * ========================================================================== */
static uint8_t crc8_update(uint8_t crc, uint8_t byte)
{
    uint8_t j;

    crc ^= byte;
    for (j = 0u; j < 8u; j++)
    {
        if ((crc & 0x80u) != 0u)
        {
            crc = (uint8_t)((crc << 1) ^ 0x07u);
        }
        else
        {
            crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

uint8_t Proto_Crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0u;
    uint8_t i;

    for (i = 0u; i < len; i++)
    {
        crc = crc8_update(crc, data[i]);
    }
    return crc;
}

/* ==========================================================================
 * 小端读写
 * ========================================================================== */
int16_t Proto_GetI16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint16_t Proto_GetU16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void Proto_PutI16(uint8_t *p, int16_t v)
{
    uint16_t u = (uint16_t)v;
    p[0] = (uint8_t)(u & 0xFFu);
    p[1] = (uint8_t)((u >> 8) & 0xFFu);
}

void Proto_PutU16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

void Proto_PutU32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* ==========================================================================
 * 初始化 / 复位
 * ========================================================================== */
void Proto_Init(rm_cmd_handler_t handler)
{
    s_handler = handler;
    s_tx_seq  = 0u;
    Proto_Reset();
}

void Proto_Reset(void)
{
    s_state = PS_H0;
    s_cmd   = 0u;
    s_len   = 0u;
    s_idx   = 0u;
    s_crc   = 0u;
}

/* ==========================================================================
 * 逐字节解析
 * ========================================================================== */
uint8_t Proto_ParseByte(uint8_t byte, uint32_t now_ms)
{
    uint8_t consumed = 1u;

    /* 帧内超时保护：一帧收了一半就断了，50ms 后丢弃重来 */
    if (s_state != PS_H0)
    {
        if ((now_ms - s_last_ms) > 50u)
        {
            Proto_Reset();
        }
    }
    s_last_ms = now_ms;

    switch (s_state)
    {
        case PS_H0:
            if (byte == RM_FRAME_H0)
            {
                s_state = PS_H1;
            }
            else
            {
                consumed = 0u;      /* 不是帧头：留给文本解析器 */
            }
            break;

        case PS_H1:
            if (byte == RM_FRAME_H1)
            {
                s_state = PS_SEQ;
                s_crc   = 0u;
            }
            else if (byte == RM_FRAME_H0)
            {
                s_state = PS_H1;
            }
            else
            {
                s_state = PS_H0;
            }
            break;

        case PS_SEQ:
            s_crc   = crc8_update(s_crc, byte);
            s_state = PS_CMD;
            break;

        case PS_CMD:
            s_cmd   = byte;
            s_crc   = crc8_update(s_crc, byte);
            s_state = PS_LEN;
            break;

        case PS_LEN:
            s_len   = byte;
            s_idx   = 0u;
            s_crc   = crc8_update(s_crc, byte);
            if (s_len > RM_FRAME_MAX_PAYLOAD)
            {
                s_state = PS_H0;
            }
            else if (s_len == 0u)
            {
                s_state = PS_CRC;
            }
            else
            {
                s_state = PS_PAYLOAD;
            }
            break;

        case PS_PAYLOAD:
            s_buf[s_idx] = byte;
            s_crc        = crc8_update(s_crc, byte);
            s_idx++;
            if (s_idx >= s_len)
            {
                s_state = PS_CRC;
            }
            break;

        case PS_CRC:
            if (byte == s_crc)
            {
                if (s_handler != 0)
                {
                    s_handler(s_cmd, s_buf, s_len);
                }
            }
            s_state = PS_H0;
            break;

        default:
            s_state = PS_H0;
            break;
    }

    return consumed;
}

/* ==========================================================================
 * 组帧
 * ========================================================================== */
uint8_t Proto_BuildFrame(uint8_t *out, uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint8_t crc = 0u;
    uint8_t i;

    if (len > RM_FRAME_MAX_PAYLOAD)
    {
        len = RM_FRAME_MAX_PAYLOAD;
    }

    out[0] = RM_FRAME_H0;
    out[1] = RM_FRAME_H1;
    out[2] = s_tx_seq;
    out[3] = cmd;
    out[4] = len;

    crc = crc8_update(crc, s_tx_seq);
    crc = crc8_update(crc, cmd);
    crc = crc8_update(crc, len);

    for (i = 0u; i < len; i++)
    {
        out[5u + i] = payload[i];
        crc         = crc8_update(crc, payload[i]);
    }

    out[5u + len] = crc;

    s_tx_seq++;

    return (uint8_t)(RM_FRAME_OVERHEAD + len);
}

