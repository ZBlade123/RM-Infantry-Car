#ifndef __LTR381_H__
#define __LTR381_H__

#include "main.h"

// 传感器I2C地址（7位地址，需根据硬件确认）
#define LTR381_I2C_ADDR 0x53
// I2C超时时间（单位：ms）
#define LTR381_I2C_TIMEOUT 100

// 寄存器地址（需与传感器 datasheet 匹配）
// 寄存器地址
#define LTR381_MAIN_CTRL 0x00
#define LTR381_MEAS_RATE 0x04
#define LTR381_GAIN 0x05
#define LTR381_PART_ID 0x06
#define LTR381_MAIN_STATUS 0x07
#define LTR381_INT_CFG 0x19

// 数据寄存器地址（核心修正）
#define LTR381_IR_DATA_0 0x0A    // IR数据低字节（LSB）
#define LTR381_IR_DATA_1 0x0B    // IR数据中字节
#define LTR381_IR_DATA_2 0x0C    // IR数据高字节（MSB）
#define LTR381_GREEN_DATA_0 0x0D // 绿色/ALS数据低字节
#define LTR381_GREEN_DATA_1 0x0E // 绿色/ALS数据中字节
#define LTR381_GREEN_DATA_2 0x0F // 绿色/ALS数据高字节
#define LTR381_RED_DATA_0 0x10   // 红色数据低字节
#define LTR381_RED_DATA_1 0x11   // 红色数据中字节
#define LTR381_RED_DATA_2 0x12   // 红色数据高字节
#define LTR381_BLUE_DATA_0 0x13  // 蓝色数据低字节
#define LTR381_BLUE_DATA_1 0x14  // 蓝色数据中字节
#define LTR381_BLUE_DATA_2 0x15  // 蓝色数据高字节

// 模式定义
#define LTR381_MODE_ALS 0x02 // ALS模式（0x0D~0x0F为环境光数据）
#define LTR381_MODE_RGB 0x06 // RGB模式（0x0D~0x0F为绿色数据）

// 增益设置
#define LTR381_GAIN_1X 0x00
#define LTR381_GAIN_3X 0x01
#define LTR381_GAIN_6X 0x02
#define LTR381_GAIN_9X 0x03
#define LTR381_GAIN_18X 0x04

// 分辨率/测量速率
#define LTR381_RESOLUTION_25MS 0x00
#define LTR381_RESOLUTION_50MS 0x41
#define LTR381_RESOLUTION_100MS 0x22

// 传感器句柄
typedef struct
{
    uint8_t mode;       // 当前工作模式
    uint8_t gain;       // 当前增益
    uint8_t resolution; // 当前分辨率
} LTR381_HandleTypeDef;

extern LTR381_HandleTypeDef ltr381;

// 函数声明
void LTR381_Init(void);
uint8_t LTR381_Config(LTR381_HandleTypeDef *hltr, uint8_t mode, uint8_t gain, uint8_t resolution);
uint8_t LTR381_ReadALS_IR(LTR381_HandleTypeDef *hltr, uint32_t *als_data, uint32_t *ir_data);
uint8_t LTR381_ReadRGB_IR(LTR381_HandleTypeDef *hltr, uint32_t *red, uint32_t *green, uint32_t *blue, uint32_t *ir);
void LTR381_LED_ON(void);
void LTR381_LED_OFF(void);
void LTR381_CalibrateRGB(uint32_t raw_r, uint32_t raw_g, uint32_t raw_b, uint32_t raw_ir,
                         uint32_t *calib_r, uint32_t *calib_g, uint32_t *calib_b);
float LTR381_GetLux_Calibrated(LTR381_HandleTypeDef *hltr, uint32_t raw_als, uint32_t raw_ir);

void LTR381_test(void);
uint8_t LTR381_ReadALS(LTR381_HandleTypeDef *hltr, uint32_t *als_data);
uint8_t LTR381_ReadRGB(LTR381_HandleTypeDef *hltr, uint32_t *red, uint32_t *green, uint32_t *blue);
#endif /* __LTR381_H__ */
