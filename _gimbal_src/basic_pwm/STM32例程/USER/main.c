/*
	*芯片说明：STM32F103RCT6/STM32F103C8T6  倍频 72M
	*控制器说明：目前测试松甲CF1和RF1代码能正常跑通，移植的时候注意PWM舵机引脚是否正确
	*例程功能说明：本例程可以通过串口控制PWM舵机，动作组等功能需要下载进阶教程
	*串口说明：本例程使用UART1和UART3，UART1用于控制器与上位机通信；UART3用于蓝牙通信
*/



#include "main.h"

void uart_frame_process(void);
void SWJ_gpio_init(void);

int main(void)
{
	uart1_init(115200); 
	uart3_init(115200); 
	SWJ_gpio_init();
	pwmServo_init();
	 while (1)
    {
				uart_frame_process();		
    }
}

void uart_frame_process(void)
{
    uint8_t uart_num;
    for(uart_num = 0; uart_num < UART_NUM_MAX; uart_num++)
    {
        if(uart_get_ok[uart_num] == 1)
        {
            uart_get_ok[uart_num] = 0; // 立刻清除标志，准备下一帧

            char *buf = uart_receive_buf[uart_num];
            uint8_t id;
            int pwm, time;
						//提取指令里的参数
            int ret = sscanf(buf, "#%hhuP%dT%d!", &id, &pwm, &time);
            if(ret == 3)
            {
							//控制PWM舵机
							pwmServo_angle_set(id, pwm, time);
							printf("%s",(char*)uart_receive_buf[uart_num]);                
            }
        }
    }
}

/* SWJ引脚配置 */
void SWJ_gpio_init(void)
{
    /**********************
    1.执行端口重映射时,复用功能时钟得使能:RCC_APB2Periph_AFIO

    2.  &1.GPIO_Remap_SWJ_Disable: !< Full SWJ Disabled (JTAG-DP + SW-DP)
         此时PA13|PA14|PA15|PB3|PB4都可作为普通IO用了
       为了保存某些调试端口,GPIO_Remap_SWJ_Disable也可选择为下面两种模式：

        &2.GPIO_Remap_SWJ_JTAGDisable: !< JTAG-DP Disabled and SW-DP Enabled
        此时PA15|PB3|PB4可作为普通IO用了

        &3.GPIO_Remap_SWJ_NoJTRST: !< Full SWJ Enabled (JTAG-DP + SW-DP) but without JTRST
        此时只有PB4可作为普通IO用了
    **********************/
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE); // 使能 PA 端口时钟
    //GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);                                               // 使能禁止JTAG和SW-DP
   GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE); // 使能禁止JTAG开启SW-DP
}

