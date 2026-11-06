/*
 * @文件描述:
 * @作者: Q
 * @Date: 2023-02-13 16:09:51
 * @LastEditTime: 2023-03-23 10:39:35
 */
#ifndef _APP_SENSOR_H_
#define _APP_SENSOR_H_
#include "main.h"

void app_sensor_init(void);
void app_sensor_run(void);
void AI_yanse_shibie(void);
void AI_dingju_jiaqu(void);
void beep_SENon(void);
void AI_chumo_init(void);              /* 触摸传感器静态识别初始化 */
void AI_hongwai_init(void);            /* 红外传感器静态识别初始化 */
void AI_shengyin_init(void);           /* 声音传感器静态识别初始化 */



void AI_chumo_jiaqu(void);
void AI_shengyin_jiaqu(void);
void AI_hongwai_jiaqu(void);
void beep_on(int times, int delay,int ARR);

#define shengyin_Read() GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_4)

#define chumo_READ() GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_7)

#define hongwai_READ() GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_5)

#endif
