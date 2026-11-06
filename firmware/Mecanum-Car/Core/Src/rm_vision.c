/**
  ******************************************************************************
  * @file    rm_vision.c
  * @brief   视觉模块对接实现
  *
  * 设计取舍（懒但够用）：
  *   1) 只收不发。模块自己算目标，我们不需要告诉它任何东西，省掉半双工那堆坑。
  *   2) 两种输入都支持：模块给像素偏差就用积分闭环（a），给绝对角就直接跟（t）。
  *      比赛方模块输出格式确定了，只改 vision_parse_line()。
  *   3) 丢目标保持不动。摄像头抖一下就把炮口甩回中位，比不动更糟。
  *   4) 解析和瞄准步长是纯函数，不碰 HAL，能在 PC 上跑 tests/vision_test.c。
  ******************************************************************************
  */

#include "rm_vision.h"
#include "rm_config.h"
#include <string.h>

/* ==========================================================================
 * 时间源（主机测试给假时钟）
 * ========================================================================== */
#if defined(RM_HOST_TEST)
static uint32_t s_fake_now;

void Vision_SetNow(uint32_t ms)
{
    s_fake_now = ms;
}

static uint32_t vision_now(void)
{
    return s_fake_now;
}
#else
static uint32_t vision_now(void)
{
    return HAL_GetTick();
}
#endif

/* ==========================================================================
 * 状态
 * ========================================================================== */
#define VISION_LINE_MAX  32

static int16_t  s_dx;
static int16_t  s_dy;
static int16_t  s_yaw10;
static int16_t  s_pitch10;
static uint8_t  s_mode;
static uint8_t  s_aim;
static uint32_t s_last_ms;
static uint32_t s_frames;

static char     s_line[VISION_LINE_MAX];
static uint8_t  s_len;
static uint8_t  s_overflow;
static char     s_last_line[VISION_LINE_MAX];   /* 最近一帧原文，给 g 指令排查协议用 */

/* ==========================================================================
 * 解析：全在这里，模块协议不一样只改这一个函数
 *   a <dx> <dy> / t <yaw10> <pitch10> / x
 * ========================================================================== */
static int32_t parse_int(const char **p)
{
    int32_t v = 0;
    int32_t sign = 1;

    while ((**p == ' ') || (**p == '\t') || (**p == ','))
    {
        (*p)++;
    }

    if (**p == '-')
    {
        sign = -1;
        (*p)++;
    }
    else if (**p == '+')
    {
        (*p)++;
    }

    if ((**p < '0') || (**p > '9'))
    {
        return 0;
    }

    while ((**p >= '0') && (**p <= '9'))
    {
        v = (v * 10) + (int32_t)(**p - '0');
        if (v > 30000)
        {
            v = 30000;
        }
        (*p)++;
    }

    return v * sign;
}

/* 相对偏差统一入口：夹限幅 + 打时间戳 */
static void set_rel_px(int32_t dx, int32_t dy)
{
    s_dx = (int16_t)((dx > 10000) ? 10000 : ((dx < -10000) ? -10000 : dx));
    s_dy = (int16_t)((dy > 10000) ? 10000 : ((dy < -10000) ? -10000 : dy));
    s_mode = RM_VISION_MODE_REL;
    s_last_ms = vision_now();
    s_frames++;
}

static void vision_parse_line(const char *p)
{
    uint8_t kind;
    char    first;

    while ((*p == ' ') || (*p == '\t') || (*p == ',') ||
           (*p == '$') || (*p == '#') || (*p == ':'))
    {
        p++;
    }

    first = *p;

    /* 只发两个数字 "42 -13" / "-25,60" 的模块也认（很多 K210/OpenMV 例程这样输出）。
     * 必须在取 kind 之前处理，否则开头的 '-' 会被当成指令字母吃掉。 */
    if (((first >= '0') && (first <= '9')) || (first == '-') || (first == '+'))
    {
        int32_t dx = parse_int(&p);
        int32_t dy = parse_int(&p);

        set_rel_px(dx, dy);
        return;
    }

    kind = (uint8_t)((*p >= 'A') && (*p <= 'Z') ? (*p + 32) : *p);
    if (kind == 0u)
    {
        return;
    }

    p++;

    if (kind == 'a')
    {
        int32_t dx = parse_int(&p);
        int32_t dy = parse_int(&p);

        set_rel_px(dx, dy);
    }
    else if (kind == 't')
    {
        int32_t y = parse_int(&p);
        int32_t pi = parse_int(&p);

        s_yaw10 = (int16_t)((y > 3000) ? 3000 : ((y < -3000) ? -3000 : y));
        s_pitch10 = (int16_t)((pi > 3000) ? 3000 : ((pi < -3000) ? -3000 : pi));
        s_mode = RM_VISION_MODE_ABS;
        s_last_ms = vision_now();
        s_frames++;
    }
    else if ((kind == 'x') || (kind == 'n'))
    {
        s_mode = RM_VISION_MODE_NONE;
    }
    else
    {
        /* 不认识的行直接丢，别让脏数据把炮口甩走 */
    }
}

void Vision_FeedChar(char c)
{
    if ((c == '\r') || (c == '\n'))
    {
        if (s_len > 0u)
        {
            /* 存原文（含超长行，排查协议时最有用），再解析 */
            {
                uint8_t i;
                uint8_t n = (uint8_t)(s_len < (uint8_t)(VISION_LINE_MAX - 1u)
                                      ? s_len : (uint8_t)(VISION_LINE_MAX - 1u));
                for (i = 0u; i < n; i++)
                {
                    s_last_line[i] = s_line[i];
                }
                s_last_line[n] = '\0';
            }

            if (s_overflow == 0u)
            {
                s_line[s_len] = '\0';
                vision_parse_line(s_line);
            }
        }

        s_len = 0u;
        s_overflow = 0u;
        return;
    }

    if (s_len < (uint8_t)(VISION_LINE_MAX - 1u))
    {
        s_line[s_len++] = c;
    }
    else
    {
        s_overflow = 1u;      /* 超长行丢掉，防止粘包把下一帧吃进解析器 */
    }
}

void Vision_Reset(void)
{
    s_dx = 0;
    s_dy = 0;
    s_yaw10 = 0;
    s_pitch10 = 0;
    s_mode = RM_VISION_MODE_NONE;
    s_len = 0u;
    s_overflow = 0u;
}

/* ==========================================================================
 * 自动瞄准：一步只挪一点点，剩下的交给舵机斜坡
 * ========================================================================== */
static int32_t clamp_step(int32_t v)
{
    if (v > RM_VISION_MAX_STEP_DEG10)
    {
        return RM_VISION_MAX_STEP_DEG10;
    }

    if (v < -RM_VISION_MAX_STEP_DEG10)
    {
        return -RM_VISION_MAX_STEP_DEG10;
    }

    return v;
}

int32_t Vision_AimStep(int32_t yaw10, int32_t pitch10,
                       int32_t *out_yaw10, int32_t *out_pitch10)
{
    int32_t dx;
    int32_t dy;
    int32_t sy;
    int32_t sp;

    if ((out_yaw10 == 0) || (out_pitch10 == 0))
    {
        return 0;
    }

    *out_yaw10 = yaw10;
    *out_pitch10 = pitch10;

    if ((s_aim == 0u) || (s_mode == RM_VISION_MODE_NONE))
    {
        return 0;
    }

    if (s_mode == RM_VISION_MODE_ABS)
    {
        *out_yaw10 = s_yaw10;
        *out_pitch10 = s_pitch10;
        return 1;
    }

    /* 相对模式：先扣死区，再按比例转成角度步长 */
    dx = s_dx;
    if ((dx <= RM_VISION_DEADBAND_PX) && (dx >= -RM_VISION_DEADBAND_PX))
    {
        dx = 0;
    }
    else
    {
        dx -= (dx > 0) ? RM_VISION_DEADBAND_PX : -RM_VISION_DEADBAND_PX;
    }

    dy = s_dy;
    if ((dy <= RM_VISION_DEADBAND_PX) && (dy >= -RM_VISION_DEADBAND_PX))
    {
        dy = 0;
    }
    else
    {
        dy -= (dy > 0) ? RM_VISION_DEADBAND_PX : -RM_VISION_DEADBAND_PX;
    }

    sy = clamp_step(dx * RM_VISION_KP_DEG10_PER_PX * RM_VISION_YAW_SIGN);
    sp = clamp_step(dy * RM_VISION_KP_DEG10_PER_PX * RM_VISION_PITCH_SIGN);

    *out_yaw10 = yaw10 + sy;
    *out_pitch10 = pitch10 - sp;        /* 目标在画面下方 -> Pitch 减小（低头） */

    return ((sy != 0) || (sp != 0)) ? 1 : 0;
}

/* ==========================================================================
 * 状态查询
 * ========================================================================== */
uint8_t Vision_IsValid(void)
{
    if (s_mode == RM_VISION_MODE_NONE)
    {
        return 0u;
    }

    return ((vision_now() - s_last_ms) < RM_VISION_TIMEOUT_MS) ? 1u : 0u;
}

uint8_t Vision_GetMode(void)
{
    return Vision_IsValid() ? s_mode : (uint8_t)RM_VISION_MODE_NONE;
}

int16_t Vision_GetDx(void)
{
    return s_dx;
}

int16_t Vision_GetDy(void)
{
    return s_dy;
}

uint32_t Vision_GetFrameCount(void)
{
    return s_frames;
}

const char *Vision_GetLastLine(void)
{
    return s_last_line;
}

void Vision_SetAim(uint8_t on)
{
    s_aim = (on != 0u) ? 1u : 0u;
}

uint8_t Vision_GetAim(void)
{
    return s_aim;
}

/* ==========================================================================
 * 硬件部分（主机测试不编进来）
 * ========================================================================== */
#if !defined(RM_HOST_TEST) && (RM_ENABLE_VISION != 0)

#include "main.h"
#include "rm_servo.h"

UART_HandleTypeDef huart3;              /* stm32f1xx_it.c 里要用，所以是全局 */
static UART_HandleTypeDef *s_uart;
static uint8_t s_rx_byte;

void Vision_RxCplt(void)
{
    if (s_uart != 0)
    {
        Vision_FeedChar((char)s_rx_byte);           /* 喂刚收到的字节 */
        (void)HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1u);
    }
}

uint8_t Vision_IsUart(const void *instance)
{
    return ((s_uart != 0) && (instance == (const void *)s_uart)) ? 1u : 0u;
}

void Vision_Init(void)
{
    GPIO_InitTypeDef g;

    Vision_Reset();
    s_aim = RM_VISION_AIM_DEFAULT;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    /* PB11 = USART3_RX，上拉输入（模块没电时不会读到浮空噪声） */
    g.Pin   = GPIO_PIN_11;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);

    huart3.Instance          = USART3;
    huart3.Init.BaudRate     = RM_VISION_BAUD;
    huart3.Init.WordLength   = UART_WORDLENGTH_8B;
    huart3.Init.StopBits     = UART_STOPBITS_1;
    huart3.Init.Parity       = UART_PARITY_NONE;
    huart3.Init.Mode         = UART_MODE_RX;
    huart3.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    (void)HAL_UART_Init(&huart3);

    s_uart = &huart3;
    (void)HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1u);

    HAL_NVIC_SetPriority(USART3_IRQn, 6u, 0u);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

void Vision_Task(void)
{
    int32_t yaw10;
    int32_t pitch10;
    int32_t n_yaw;
    int32_t n_pitch;

    if (s_aim == 0u)
    {
        return;
    }

    if (Vision_IsValid() == 0u)
    {
        return;      /* 丢目标保持不动，别甩回中位 */
    }

    yaw10   = (int32_t)Yaw_GetTargetDeg10();
    pitch10 = (int32_t)Servo_GetTargetDeg10();

    if (Vision_AimStep(yaw10, pitch10, &n_yaw, &n_pitch) != 0)
    {
        Yaw_SetTargetDeg10((int16_t)n_yaw);        /* 限位/斜坡在 rm_servo 里 */
        Servo_SetTargetDeg10((int16_t)n_pitch);
    }
}

#else

/* 主机测试或关掉视觉模块时的空实现，保证链接不报 undefined */
void Vision_Init(void)
{
    Vision_Reset();
    s_aim = 0u;
}

void Vision_Task(void)
{
}

void Vision_RxCplt(void)
{
}

uint8_t Vision_IsUart(const void *instance)
{
    (void)instance;
    return 0u;
}

#endif /* !RM_HOST_TEST && RM_ENABLE_VISION */
