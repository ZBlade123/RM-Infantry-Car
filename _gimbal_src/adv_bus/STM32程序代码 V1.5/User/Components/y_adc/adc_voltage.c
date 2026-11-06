#include "./y_adc/adc_voltage.h"

/**
 * @brief  ADC与GPIO初始化（PA6模拟输入，ADC1单通道单次转换）
 */
void ADC_Voltage_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;

    // 步骤1：使能时钟（GPIO和ADC）
    RCC_APB2PeriphClockCmd(ADC_GPIO_CLK | ADCx_CLK, ENABLE);

    // 步骤2：配置PA6为模拟输入（无上下拉，无推挽）
    GPIO_InitStructure.GPIO_Pin = ADC_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;  // 模拟输入模式
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ADC_GPIO_PORT, &GPIO_InitStructure);

    // 步骤3：配置ADC基本参数
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;  // 独立ADC模式
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;       // 单通道（非扫描）
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE; // 单次转换（非连续）
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None; // 软件触发
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right; // 12位数据右对齐
    ADC_InitStructure.ADC_NbrOfChannel = 1; // 转换通道数：1
    ADC_Init(ADCx, &ADC_InitStructure);

    // 步骤4：配置ADC通道6的采样时间（高阻抗分压电路需长采样）
    // 采样时间：239.5个ADC时钟周期（确保40kΩ电阻下采样电容充满）
    ADC_RegularChannelConfig(ADCx, ADCx_CHANNEL, 1, ADC_SampleTime_239Cycles5);

    // 步骤5：使能ADC并校准（校准是ADC高精度的关键）
    ADC_Cmd(ADCx, ENABLE);          // 使能ADC
    ADC_ResetCalibration(ADCx);     // 重置校准寄存器
    while(ADC_GetResetCalibrationStatus(ADCx)); // 等待重置完成
    ADC_StartCalibration(ADCx);     // 开始校准
    while(ADC_GetCalibrationStatus(ADCx));     // 等待校准完成
}

/**
 * @brief  获取单次ADC原始采样值
 * @return 12位ADC值（范围：0~4095）
 */
uint16_t ADC_Get_RawValue(void)
{
    // 1. 启动ADC单次转换
    ADC_SoftwareStartConvCmd(ADCx, ENABLE);

    // 2. 等待转换完成（ADC_FLAG_EOC：转换结束标志）
    while(!ADC_GetFlagStatus(ADCx, ADC_FLAG_EOC));

    // 3. 清除转换完成标志，返回采样值
    ADC_ClearFlag(ADCx, ADC_FLAG_EOC);
    return ADC_GetConversionValue(ADCx);
}

/**
 * @brief  多次采样取平均值（降低干扰）
 * @param  n：采样次数（建议取10、20等，次数越多越稳定）
 * @return 平均ADC值（0~4095）
 */
uint16_t ADC_Get_AvgValue(uint8_t n)
{
    uint32_t sum = 0;  // 用32位变量存总和，避免溢出
    uint8_t i;

    for(i = 0; i < n; i++)
    {
        sum += ADC_Get_RawValue();
        delay_us(10);  // 采样间隔（避免连续采样导致的温漂）
    }

    return (uint16_t)(sum / n);  // 返回平均值
}

/**
 * @brief  计算实际输入电压
 * @return 输入电压（单位：V，保留2位小数）
 * @note   ADC参考电压默认3.3V（STM32F1内核供电）
 */
float ADC_Calc_InputVoltage(void)
{
    uint16_t adc_avg;
    float adc_voltage;  // PA6的采样电压
    float input_voltage; // 实际输入电压

    // 1. 获取20次采样平均值（平衡精度与速度）
    adc_avg = ADC_Get_AvgValue(20);

    // 2. 计算PA6的采样电压：ADC值/4095 × 参考电压（3.3V）
    adc_voltage = (float)adc_avg / 4095.0f * 3.25f;

    // 3. 按分压比例计算实际输入电压（×4）
    input_voltage = adc_voltage * 4.0f;

    return input_voltage;
}
