#include "app_sensor.h"

/**
 * @函数描述: 传感器相关设备控制初始化
 * @return {*}
 */
void app_sensor_init(void)
{
	TIMER_Configuration();
	led_init();
	beep_init();				//蜂鸣器初始化
	ADC_Voltage_Init();
	beep_on(3,100,500);

	OLED_Init();
	//OLED_TEST();
	LTR381_Init();
	AI_shengyin_init();
	AI_hongwai_init();
	AI_chumo_init();
	US01_Init();
	//us01_rgb_r(0,255,0);
	//us01_rgb_t(0,255,0);
	us01_rgb_both((Color_t)COLOR_GREEN);
}

/**
 * @函数描述: 循环检测输出传感器引脚的AD值
 * @return {*}
 */
void app_sensor_run(void)
{
    static u8 AI_mode_bak;

    // 有动作执行，直接返回
    if (group_do_ok == 0)
        return;
	
    if (AI_mode == 0)
    {
    }
    else if (AI_mode == 1) 
    {
			AI_yanse_shibie(); /* 颜色识别 */
    }
    else if (AI_mode == 2)
    {
      AI_dingju_jiaqu()  ; 
    }
		else if (AI_mode == 3)
    {
      AI_hongwai_jiaqu() ; 
    }
		else if (AI_mode == 4)
    {
      AI_shengyin_jiaqu() ; 
    }
    else if (AI_mode == 6)
    {
      AI_chumo_jiaqu() ; 
    }

    else if (AI_mode == 10)
    {
        AI_mode = 255;
    }

    if (AI_mode_bak != AI_mode)
    {
        AI_mode_bak = AI_mode;
        group_do_ok = 1;
    }
}
/*************************************************************
函数名称：AI_yanse_shibie()
功能介绍：识别木块颜色，夹取分别放到不同位置
函数参数：无
返回值：  无
******************************************************** *****/
void AI_yanse_shibie(void)
{
    static u32 systick_ms_yanse = 0;
    uint32_t raw_r, raw_g, raw_b, raw_als;

    if (group_do_ok && millis() - systick_ms_yanse > 500)
    {
        systick_ms_yanse = millis();
			//LTR381_test();
				raw_als = (LTR381_ReadRGB_IR(&ltr381, &raw_r, &raw_g, &raw_b,&raw_als) != 0) ? raw_als : 0;
      if (raw_als <= 60 && raw_als >= 10) {  
			if (LTR381_ReadRGB(&ltr381, &raw_r, &raw_g, &raw_b) == 1)
        {
            if (raw_r > raw_g && raw_r > raw_b && raw_r > 1000)
            {
							 us01_set_color(0);
               parse_cmd("$DGT:1-9,1!"); // 执行脱机存储动作组
							
            }
            else if (raw_g > raw_r && raw_g > raw_b && raw_b > raw_r && raw_g >= 1400 && raw_b <= 1000)
            {
							 us01_set_color(1);
               parse_cmd("$DGT:10-19,1!"); // 执行脱机存储动作组
            }
            else if (raw_b > raw_r && raw_b > raw_g && raw_b > 1000)
            {
							 us01_set_color(2);
               parse_cmd("$DGT:20-29,1!"); // 执行脱机存储动作组
            }
        }
			}
    }
}
/*************************************************************
函数名称：AI_dingju_jiaqu()
功能介绍：识别物体距离夹取物体
函数参数：无
返回值：  无
*************************************************************/
void AI_dingju_jiaqu(void)
{
	  static u32 systick_ms_bak = 0;
    float adc_csb;
		us01_rgb_both((Color_t)COLOR_OFF);
    if (group_do_ok == 0)
        return;
		if (millis() - systick_ms_bak > 50)
    {
			systick_ms_bak = millis();
			us01_start_measuring();
			delay_ms(100);
			adc_csb = us01_read_distance(); // 获取a0的ad值，计算出距离
			
			//printf("DISTANCE: %f cm",adc_csb);
    if ((adc_csb > 13.0) && (adc_csb <= 15.0))
    {
        beep_SENon();
        parse_cmd("$DGT:30-38,1!");
    }
    
	}
}

/*************************************************************
函数名称：AI_dingju_jiaqu()
功能介绍：识别物体距离夹取物体
函数参数：无
返回值：  无
*************************************************************/
void AI_chumo_jiaqu(void)
{
   // 静态识别夹取
        if (chumo_READ() == 0) /* 如果要用触摸传感器请判断为1 */
        {
            delay_ms(50);
            if (chumo_READ() == 0)
            {
                parse_cmd("$DGT:30-38,1!");
								beep_SENon();
           
            }
        }
}
void AI_hongwai_jiaqu(void)
{
   // 静态识别夹取
        if (hongwai_READ() == 0) /* 如果要用触摸传感器请判断为1 */
        {
            delay_ms(50);
            if (hongwai_READ() == 0)
            {
                parse_cmd("$DGT:10-19,1!");
								beep_SENon();
           
            }
        }
}
void AI_shengyin_jiaqu(void)
{
   // 声控夹取
        if (shengyin_Read() == 0) /* 如果要用触摸传感器请判断为1 */
        {
           parse_cmd("$DGT:10-19,1!");
					 beep_SENon();
        }
}

void beep_SENon(void){
	
	TIM_PrescalerConfig(TIM2, 2000, TIM_PSCReloadMode_Immediate); // PSC = 4999
	
	TIM_Cmd(TIM2, ENABLE);
	delay_ms(100);
	TIM_Cmd(TIM2, DISABLE);
	GPIO_WriteBit(GPIOC,GPIO_Pin_6,Bit_RESET);

}

void beep_on(int times, int delay,int ARR)
{
	TIM_PrescalerConfig(TIM2, ARR, TIM_PSCReloadMode_Immediate); // PSC = 4999
    int i;
    for (i = 0; i < times; i++)
    {
        TIM_Cmd(TIM2, ENABLE);
        delay_ms(delay);
        TIM_Cmd(TIM2, DISABLE);
        delay_ms(delay);
    }
		GPIO_WriteBit(GPIOC,GPIO_Pin_6,Bit_RESET);
}

void AI_chumo_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD; /* 如果要用触摸传感器请判断为设为下拉GPIO_Mode_IPD */
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void AI_hongwai_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}

void AI_shengyin_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}
