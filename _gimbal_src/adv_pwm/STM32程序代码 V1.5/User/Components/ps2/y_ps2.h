#ifndef _Y_PS2_H_
#define _Y_PS2_H_
#include "main.h"

extern u8 ps2_buf[9];

/*******PS2相关函数声明*******/
uint8_t usb_ps2_Init(void);       /* PS2手柄初始化 */
void ps2_write_read(void); /* 读取手柄数据 */
uint8_t ps2_is_connected(void);
#endif
