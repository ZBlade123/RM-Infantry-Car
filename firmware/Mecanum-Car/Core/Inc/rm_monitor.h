/**
  ******************************************************************************
  * @file    rm_monitor.h
  * @brief   系统状态监控：电池电压、上电时长、欠压标志
  ******************************************************************************
  */

#ifndef __RM_MONITOR_H
#define __RM_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
    uint16_t vbat_mv;       /* 电池电压 mV（没开电压检测时恒为 0） */
    uint8_t  vbat_valid;    /* 1 = 电压读数有效 */
    uint8_t  low_voltage;   /* 1 = 低于报警阈值 */
    uint32_t uptime_ms;
} rm_status_t;

void Monitor_Init(void);
void Monitor_Task(void);                /* 100ms 调用 */
const rm_status_t *Monitor_Get(void);

#ifdef __cplusplus
}
#endif

#endif /* __RM_MONITOR_H */
