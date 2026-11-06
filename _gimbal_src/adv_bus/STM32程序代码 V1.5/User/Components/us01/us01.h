#ifndef __US01_H
#define __US01_H
#include "main.h"

// 颜色定义
#define COLOR_RED     {255, 0, 0}
#define COLOR_GREEN   {0, 255, 0}
#define COLOR_BLUE    {0, 0, 255}
#define COLOR_YELLOW  {255, 255, 0}
#define COLOR_PURPLE  {255, 0, 255}
#define COLOR_CYAN    {0, 255, 255}
#define COLOR_WHITE   {255, 255, 255}
#define COLOR_OFF     {0, 0, 0}

// 颜色结构体定义
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color_t;

uint8_t US01_Init(void);
uint8_t us01_rgb_r(uint8_t r,uint8_t g,uint8_t b);
uint8_t us01_rgb_t(uint8_t r,uint8_t g,uint8_t b);
uint8_t us01_rgb_both(Color_t color);
uint8_t us01_start_measuring(void);
float us01_read_distance(void);
void us01_set_color(uint8_t index);
#endif
