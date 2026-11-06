/**
  ******************************************************************************
  * @file    rm_crc.c
  * @brief   CRC 工具实现（逐位版本，省 flash）
  ******************************************************************************
  */

#include "rm_crc.h"

uint8_t RM_Crc8(uint8_t init, const uint8_t *data, uint16_t len)
{
    uint8_t  crc = init;
    uint16_t i;
    uint8_t  b;

    if (data == 0)
    {
        return crc;
    }

    for (i = 0u; i < len; i++)
    {
        crc ^= data[i];
        for (b = 0u; b < 8u; b++)
        {
            if ((crc & 0x80u) != 0u)
            {
                crc = (uint8_t)((uint8_t)(crc << 1) ^ 0x31u);
            }
            else
            {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t RM_Crc16(uint16_t init, const uint8_t *data, uint16_t len)
{
    uint16_t crc = init;
    uint16_t i;
    uint8_t  b;

    if (data == 0)
    {
        return crc;
    }

    for (i = 0u; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (b = 0u; b < 8u; b++)
        {
            if ((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}
