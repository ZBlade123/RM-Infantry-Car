#ifndef __ADC_VOLTAGE_H
#define __ADC_VOLTAGE_H

#include "stm32f10x.h"
#include "main.h"

// 1. 引脚与ADC硬件定义
#define ADCx                    ADC1
#define ADCx_CLK                RCC_APB2Periph_ADC1
#define ADCx_CHANNEL            ADC_Channel_6  // PA6对应ADC1通道6

#define ADC_GPIO_PORT           GPIOA
#define ADC_GPIO_PIN            GPIO_Pin_6
#define ADC_GPIO_CLK            RCC_APB2Periph_GPIOA

// 2. 函数声明
void ADC_Voltage_Init(void);          // ADC初始化（含GPIO配置）
uint16_t ADC_Get_RawValue(void);      // 获取单次ADC原始值（12位：0~4095）
uint16_t ADC_Get_AvgValue(uint8_t n); // 获取n次采样平均值（抗干扰）
float ADC_Calc_InputVoltage(void);    // 计算实际输入电压（单位：V）
 
#endif
