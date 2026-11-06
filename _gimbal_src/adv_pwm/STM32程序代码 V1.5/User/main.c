#include "stm32f10x.h"
#include "main.h"


/*  函数声明   */

void PS2_or_BATTARY_control(void);
void SWJ_gpio_init(void);
void app_led_run(void);
void app_OLED_run(void);
void parameter_init(void);


/**
  * @brief  主函数
  * @param  无
  * @retval 无
  */
int main(void)
{
	SysTick_Init();  		//初始化系统嘀答定时器，1ms定时一次
	SWJ_gpio_init();  
	app_uart_init();		//串口波特率初始化
	app_ps2_init();			//手柄初始化
	pwmServo_init();		//舵机初始化
	
	w25x_init();      	//W25Q64存储芯片初始化
	printf("w25q64 id=%x\n",w25x_readId());   
	delay_ms(100);
	
	parameter_init();		// 初始化配置参数	
	app_sensor_init();	//传感器与外设初始化
	setup_kinematics(100, 105, 88, 155, &kinematics);  //设置逆运动学
	while(1)
	{
		app_ps2_run();
		app_OLED_run();
		//PS2_or_BATTARY_control();
		app_uart_run(); //串口接收与解析函数
		app_led_run();	//led以一秒的频率闪烁
		app_action_run();
		app_sensor_run(); //传感器功能集合
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

/**
 * @函数描述: 循环执行工作指示灯任务运行，让LED闪烁 1s跳动一次
 * @return {*}
 */
void app_led_run(void)
{
    static u32 time_count = 0;
    if (millis() - time_count < 1000)
        return;
    time_count = millis();
    LED_TOGGLE();

}
/**
 * @函数描述: OLED屏幕按照一定频率刷新采集到的电量和视觉功能的模式
 * @return {*}
 */
void app_OLED_run(void)
{
    static u32 time_count1 = 0;
		float input_vol;
		char text[20];
		char text1[20];
	  const unsigned char *display_chars;
		int num_chars;
    if (millis() - time_count1 < 1000)
        return;
    time_count1 = millis();
		input_vol = ADC_Calc_InputVoltage();
		OLED_ClearQuarter(2);
    sprintf(text, "InV=%f", input_vol); 
		float battery_percent; // 最终电量百分比（0~100）

		if (input_vol >= 8.4) {
				battery_percent = 100;
		} else if (input_vol >= 8.2) {
				// 8.2~8.4V对应90~100%，线性插值：(电压-8.2)*50 + 90
				battery_percent = (input_vol - 8.2) * 50 + 90;
		} else if (input_vol >= 7.8) {
				// 7.8~8.2V对应50~90%，线性插值：(电压-7.8)*100 + 50
				battery_percent = (input_vol - 7.8) * 100 + 50;
		} else if (input_vol >= 7.2) {
				// 7.2~7.8V对应10~50%，线性插值：(电压-7.2)*66.7 + 10
				battery_percent = (input_vol - 7.2) * 66.7 + 10;
		} else if (input_vol >= 6.0) {
				// 6.0~7.2V对应0~10%，线性插值：(电压-6.0)*8.3 + 0
				battery_percent = (input_vol - 6.0) * 8.3;
		} else {
				battery_percent = 0;
		}
		sprintf(text1, "bat=%.2f%%", battery_percent); 
//		OLED_P6x8Str(0, 0,(uint8_t*)text);
		OLED_P6x8Str(0, 1,(uint8_t*)text1);
		switch (OLED_mode)
            {
            case 1:
                display_chars = CAR_COLOR_TRACE;
                num_chars = 6;
                break;
            case 2:
                display_chars = MAP3;
                num_chars = 5;
                break;
            case 3:
                display_chars = ARM_COLOR_TRACE;
                num_chars = 7;
                break;
            case 4:
                display_chars = AprilTag_Sort;
                num_chars = 7;
                break;	
            case 5:
                display_chars = AprilTag_Stack;
                num_chars = 7;
                break;
            case 6:
                display_chars = COLOR_TRACE_GRASP;
                num_chars = 6;
                break;
            case 7:
                display_chars = MAP1;
                num_chars = 5;
                break;
            case 8:
                display_chars = MAP2;
                num_chars = 5;
                break;	
            case 9:
                display_chars = FACE_TRACE;
                num_chars = 4;
                break;		
            case 10:
                display_chars = PTZ_COLOR_TRACE;
                num_chars = 6;
                break;
						default:
                display_chars = NULL;
                num_chars = 0;
                break;
            }
					
            if (display_chars != NULL)
            {
							if(mode_run==1)
							{
                for (int i = 0, x = 8; i < num_chars; i++, x += 16)
                {
                    OLED_P16x16Ch(x, 3, i, display_chars);
                }
								mode_run=0;
							}
            }
						else
						{
							if(mode_run==1)
							{
                for (int i = 0, x = 8; i < 5; i++, x += 16)
                {
                    OLED_P16x16Ch(x, 3, i, null);
                }
								mode_run=0;
							}
						}
}

// 初始化配置参数
void parameter_init(void)
{
    uint8_t i = 0;

    delay_ms(10);
    w25x_read((u8 *)(&eeprom_info), W25Q64_INFO_ADDR_SAVE_STR, sizeof(eeprom_info)); // 读取全局变量

    if (eeprom_info.version != VERSION) // 判断版本是否是当前版本
    {
        eeprom_info.version = VERSION; // 复制当前版本
        eeprom_info.dj_record_num = 0; // 学习动作组变量赋值0
    }
	  printf("\r\nVERSION=%d",eeprom_info.version);

    if (eeprom_info.dj_bias_pwm[SERVO_NUM] != FLAG_VERIFY)
    {
        for (i = 0; i < SERVO_NUM; i++)
        {
            eeprom_info.dj_bias_pwm[i] = 0;
        }
        eeprom_info.dj_bias_pwm[SERVO_NUM] = FLAG_VERIFY;
    }

    for (i = 0; i < SERVO_NUM; i++)
    {
		pwmServo_bias_set(i,eeprom_info.dj_bias_pwm[i]);
    }
	
    // 执行预存命令 {G0000#000P1500T1000!#000P1500T1000!}
    if (eeprom_info.pre_cmd[PRE_CMD_SIZE] == FLAG_VERIFY)
    {
        if (eeprom_info.pre_cmd[0] == '$')
        {
            parse_cmd(eeprom_info.pre_cmd);
        }
    }
}

/* 
		手柄需要实时控制，电池检测使用ADC采样
		有延迟设置，与手柄冲突，因此需要进行二选一设置
		当usb接入时，oled就不会再进行刷新
		拔掉usb之后，oled函数恢复执行

*/

void PS2_or_BATTARY_control(void)
{
    static uint32_t last_check_time = 0;
    static uint8_t ps2_connected = 0;
    
    // 每200ms检查一次连接状态
    if (millis() - last_check_time > 200)
    {
        last_check_time = millis();
        ps2_connected = ps2_is_connected();
			
    }
    
    // 根据连接状态执行不同的程序
    if (ps2_connected)
    {
        ps2_write_read(); // 读取PS2数据
        app_ps2_run();    // 执行PS2相关功能
    }
    else
    {
        app_OLED_run();   // 执行OLED显示
    }
}



void TIMER_Configuration(void) {
    TIM_TimeBaseInitTypeDef TIM_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    
    // 使能TIM2时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    
    // 配置定时器基础
    TIM_InitStructure.TIM_Period = 18 - 1;        // ARR值
    TIM_InitStructure.TIM_Prescaler = 500 - 1;   // PSC值
    TIM_InitStructure.TIM_ClockDivision = 0;
    TIM_InitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_InitStructure);
    
    // 配置更新中断
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
		TIM_Cmd(TIM2, DISABLE);
    
    // 配置NVIC
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}


//驱动无源蜂鸣器
void TIM2_IRQHandler(void) {
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        GPIOC->ODR ^= GPIO_Pin_6;  // 翻转PC6状态
    }
}


