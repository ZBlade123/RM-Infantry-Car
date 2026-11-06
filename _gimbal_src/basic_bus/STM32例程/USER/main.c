/*
	*芯片说明：STM32F103RCT6/STM32F103C8T6  倍频 72M
	*控制器说明：目前测试松甲CF1和RF1代码能正常跑通，RCT6和C8T6可以正常移植代码
	*例程功能说明：本例程可以与总线舵机通信，控制舵机工作、切换工作模式、读取数据等，具体指令查看指令说明书，执行动作组等功能需下载进阶例程
	*串口说明：本例程使用UART1、UART3和UART5，UART1用于控制器与上位机通信；UART3用于蓝牙通信；UART5用于控制器与总线舵机通信
*/



#include "main.h"

int main(void)
{
	uart1_init(115200); 
	uart3_init(115200); 
	uart5_init(115200); 

	
	 while (1)
    {
			//如果串口1接收到数据，print打印指令，串口5发送指令给舵机
			if(uart_get_ok[1]){	
				printf("%s",(char*)uart_receive_buf[1]);
				uart5_send_str((char*)uart_receive_buf[1]);
				uart_get_ok[1] = 0;
			}
			//如果串口3接收到数据，串口5发送指令给舵机
			if(uart_get_ok[3]){
				uart5_send_str((char*)uart_receive_buf[3]);
				uart_get_ok[3] = 0;
			}
			//如果串口5接收到数据，print打印指令
			if(uart_get_ok[5]){
				printf("%s",(char*)uart_receive_buf[5]);
				uart_get_ok[5] = 0;
			}
			
			
			
    }
}




