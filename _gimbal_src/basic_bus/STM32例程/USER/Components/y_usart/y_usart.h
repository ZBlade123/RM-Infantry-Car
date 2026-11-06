/****************************************************************************
 *	@笔者	：	Q
 *	@日期	：	2023年2月8日
 *	@所属	：	杭州松甲科技
 *	@功能	：	存放usart串口相关的函数
 ****************************************************************************/

#ifndef _Y_USART_H_
#define _Y_USART_H_
#include "main.h"

#define UART_NUM_MAX      6
#define UART_BUF_SIZE 64 // 定义最大接收字节数 128

extern char uart_receive_buf[UART_NUM_MAX][UART_BUF_SIZE];   // 每串口独立缓冲 // 接收缓冲,最大UART_BUF_SIZE个字节.末字节为换行符
extern uint16_t uart_get_ok[UART_NUM_MAX];            // 接收完成标记


/*******串口相关函数声明*******/
void uart1_init(uint32_t BaudRate); /* 初始化串口1 */
void uart1_send_str(char *s);         /* Usart1发送字符串 */
void uart1_send_int(int tmp);       /*	串口1发送数字 */

void uart3_init(u32 baud);
void uart3_send_str(char *s);

void uart5_init(u32 baud);
void uart5_send_str(char *s);



#endif
