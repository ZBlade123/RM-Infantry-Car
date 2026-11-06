/**
  ******************************************************************************
  * @file    rm_vision.h
  * @brief   视觉模块对接（USART3 RX=PB11 115200）
  *          - 只收不发：模块自己算目标，我们只管"把炮口转过去"
  *          - 两种输入：相对偏差 a dx dy（闭环积分）/ 绝对角 t yaw10 pitch10
  *          - 丢目标（超时或无帧）时保持当前角度，不甩头
  *          - 解析逻辑是纯函数，可以主机跑测试（tests/vision_test.c）
  ******************************************************************************
  */

#ifndef __RM_VISION_H
#define __RM_VISION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define RM_VISION_MODE_NONE   0u   /* 没有目标 */
#define RM_VISION_MODE_REL    1u   /* a dx dy：像素偏差 */
#define RM_VISION_MODE_ABS    2u   /* t yaw10 pitch10：绝对角 */

void    Vision_Init(void);
void    Vision_Task(void);                 /* 10ms：超时 + 自动瞄准执行 */

/* ---- 纯逻辑（不碰硬件，主机测试直接调） ---- */
void    Vision_FeedChar(char c);           /* 喂一个字节，内部按行拼包 */
int32_t Vision_AimStep(int32_t yaw10, int32_t pitch10,
                       int32_t *out_yaw10, int32_t *out_pitch10);
void    Vision_Reset(void);

/* ---- 状态查询 ---- */
uint8_t Vision_IsValid(void);              /* 1 = 有新鲜目标 */
uint8_t Vision_GetMode(void);
int16_t Vision_GetDx(void);
int16_t Vision_GetDy(void);
uint32_t Vision_GetFrameCount(void);
const char *Vision_GetLastLine(void);      /* 最近一帧原文，排查模块协议用 */
void    Vision_SetAim(uint8_t on);
uint8_t Vision_GetAim(void);

/* ---- 串口（RM_ENABLE_VISION=1 时才编进来） ---- */
uint8_t Vision_IsUart(const void *instance);   /* 给 rm_remote 的 RxCplt 分流 */
void    Vision_RxCplt(void);

#if defined(RM_HOST_TEST)
void    Vision_SetNow(uint32_t ms);        /* 主机测试用假时钟 */
#endif

#ifdef __cplusplus
}
#endif

#endif /* __RM_VISION_H */
