#include "y_beep/y_beep.h"


// 全局变量控制状态
volatile uint8_t buzzer_state = 0;  // 0=关闭，1=发声
volatile uint16_t toggle_count = 0; // 电平翻转计数器
volatile uint16_t high_time = 500;  // 高电平时间（占空比50%）
volatile uint16_t low_time = 500;   // 低电平时间

void TIM6_IRQHandler(void); // 声明中断函数
/**
  * @简  述  初始化
  * @参  数  无
  * @返回值  无
  */
void BEEP_Init(void) 
{

    GPIO_InitTypeDef GPIO_InitStructure;
    
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE); /* 使能端口时钟 */
	
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;            /* 配置 pin */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;  /* 推挽输出 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; /* IO 翻转 50MHz */
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // 开启TIM2时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
    
    // 配置定时器时基
    TIM_TimeBaseInitTypeDef TIM_InitStruct;
    TIM_InitStruct.TIM_Prescaler = 71;         // 预分频72MHz→1MHz
    TIM_InitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_InitStruct.TIM_Period = 1000;          // 初始ARR=1000（1kHz中断）
    TIM_InitStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM6, &TIM_InitStruct);
    
    // 使能更新中断
    TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE);
    NVIC_EnableIRQ(TIM6_IRQn);  // 启用TIM2中断通道
    
    TIM_Cmd(TIM6, ENABLE);      // 启动定时器
}

void TIM6_IRQHandler(void) {
    if (TIM_GetITStatus(TIM6, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM6, TIM_IT_Update); // 清除中断标志
        
        if (buzzer_state) {
            toggle_count++;
            
            // 翻转IO电平
            if (GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_1) == Bit_RESET) {
                GPIO_SetBits(GPIOC, GPIO_Pin_13);   // 拉高
                TIM_SetAutoreload(TIM6, high_time); // 下次中断时间为高电平时间
            } else {
                GPIO_ResetBits(GPIOC, GPIO_Pin_13);  // 拉低
                TIM_SetAutoreload(TIM6, low_time);   // 下次中断时间为低电平时间
            }
        }
    }
}

void Buzzer_Start(void) {
    buzzer_state = 1;
    toggle_count = 0;
    GPIO_SetBits(GPIOC, GPIO_Pin_13); // 初始高电平
    TIM_SetAutoreload(TIM6, high_time);
    TIM_Cmd(TIM6, ENABLE);
}

void Buzzer_Stop(void) {
    buzzer_state = 0;
    GPIO_ResetBits(GPIOC, GPIO_Pin_13); // 关闭输出
    TIM_Cmd(TIM6, DISABLE);
}

void Buzzer_Set(uint32_t freq, float duty) {
    uint32_t total_time = SystemCoreClock / 72 / freq; // 定时器时钟1MHz
    high_time = (uint16_t)(total_time * duty);
    low_time = (uint16_t)(total_time * (1 - duty));
    
    // 限制最小时间（避免中断过于频繁）
    if (high_time < 10) high_time = 10;
    if (low_time < 10) low_time = 10;
}
/******************* (C) 版权 2022 XTARK **************************************/
