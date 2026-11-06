/**
  ******************************************************************************
  * @file    rm_monitor.c
  * @brief   状态监控实现（电池电压用寄存器级 ADC，不依赖 HAL ADC 模块，
  *          这样 CubeMX 重新生成工程也不会把 ADC 初始化弄丢）
  *
  * 为什么不用 HAL ADC：这个工程本来没勾 ADC，加 HAL ADC 要重新生成工程、
  *   多链接一个 .c；而电压采集很简单，直接配 ADC1 寄存器更省事。
  *
  * 默认关闭（RM_ENABLE_VBAT=0），因为开源板没把 PB1 引出来，
  *   需要飞线 + 分压电阻，见 rm_config.h 注释和 docs/01_接线与引脚表.md。
  ******************************************************************************
  */

#include "rm_monitor.h"
#include "rm_config.h"

static rm_status_t s_status;

#if (RM_ENABLE_VBAT != 0)

/* ==========================================================================
 * PB1 = ADC1_IN9，直接操作寄存器
 * ========================================================================== */
static void adc_init(void)
{
    volatile uint32_t i;

    __HAL_RCC_ADC1_CLK_ENABLE();

    /* PB1 配成模拟输入：CRL 里 PB1 是 bit4~bit7，CNF=00 MODE=00 */
    GPIOB->CRL &= ~(0xFu << 4);

    ADC1->CR1 = 0u;
    ADC1->CR2 = 0u;

    /* 通道 9 采样时间 239.5 周期（分压电阻内阻大，采样时间要够） */
    ADC1->SMPR2 |= (ADC_SMPR2_SMP9_2 | ADC_SMPR2_SMP9_1 | ADC_SMPR2_SMP9_0);

    /* 规则组：1 次转换，第 1 个转换通道 9 */
    ADC1->SQR1 = 0u;
    ADC1->SQR2 = 0u;
    ADC1->SQR3 = RM_VBAT_ADC_CHANNEL;

    ADC1->CR2 |= ADC_CR2_ADON;
    for (i = 0u; i < 1000u; i++)
    {
        __NOP();
    }

    /* 上电后必须自校准，否则读数有几十 mV 的固定偏差 */
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    while ((ADC1->CR2 & ADC_CR2_RSTCAL) != 0u)
    {
    }
    ADC1->CR2 |= ADC_CR2_CAL;
    while ((ADC1->CR2 & ADC_CR2_CAL) != 0u)
    {
    }
}

static uint16_t adc_read_raw(void)
{
    uint32_t guard = 0u;

    ADC1->CR2 |= ADC_CR2_ADON;          /* 再次置 1 = 启动一次转换 */
    while ((ADC1->SR & ADC_SR_EOC) == 0u)
    {
        guard++;
        if (guard > 100000u)
        {
            return 0u;                  /* 超时保护，别在主循环里死等 */
        }
    }
    return (uint16_t)(ADC1->DR & 0x0FFFu);
}

static void vbat_update(void)
{
    uint32_t raw = 0u;
    uint32_t acc = 0u;
    uint8_t  i;
    uint32_t mv;

    for (i = 0u; i < 8u; i++)
    {
        acc += adc_read_raw();
    }
    raw = acc / 8u;

    /* mV = raw * Vref / 4095 * (R1+R2)/R2  —— 分两步算，避免 32 位溢出 */
    mv = (raw * RM_VBAT_REF_MV) / 4095UL;
    mv = (mv * (RM_VBAT_R_TOP + RM_VBAT_R_BOTTOM)) / RM_VBAT_R_BOTTOM;

    s_status.vbat_mv    = (uint16_t)mv;
    s_status.vbat_valid = 1u;
    s_status.low_voltage = (mv < (uint32_t)RM_VBAT_WARN_MV) ? 1u : 0u;
}

#endif /* RM_ENABLE_VBAT */

/* ==========================================================================
 * 对外接口
 * ========================================================================== */
void Monitor_Init(void)
{
    s_status.vbat_mv     = 0u;
    s_status.vbat_valid  = 0u;
    s_status.low_voltage = 0u;
    s_status.uptime_ms   = 0u;

#if (RM_ENABLE_VBAT != 0)
    adc_init();
#endif
}

void Monitor_Task(void)
{
    s_status.uptime_ms = HAL_GetTick();

#if (RM_ENABLE_VBAT != 0)
    vbat_update();
#endif
}

const rm_status_t *Monitor_Get(void)
{
    return &s_status;
}
