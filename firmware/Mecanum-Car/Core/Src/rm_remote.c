/**
  ******************************************************************************
  * @file    rm_remote.c
  * @brief   串口链路层实现
  *          - 中断里只做一件事：把字节塞进环形缓冲
  *          - Remote_Task() 在 1ms 节拍里取空缓冲，区分"帧"和"文本行"
  *          - 文本指令方便用串口助手直接敲；帧指令给上位机工具用
  ******************************************************************************
  * 文本指令表（串口助手，末尾带回车/换行，大小写均可）：
  *   v <vx> <vy> <wz>   直给底盘速度 -1000~1000, 例：v 400 0 0
  *   s <deg10>          云台 Pitch 目标角 x10，例：s 750 = 75.0°
  *   y <deg10>          云台 Yaw 目标角 x10，例：y 900 = 90.0°
  *   g                  打印云台两轴角度 + 回中状态 + 云台系驱动开关
  *   follow <0|1>       云台系驱动：1 = 前进沿炮口方向（瞄准时横移不带偏炮口）
  *   aim <0|1>          自动瞄准（v3.4）：1 = 按视觉模块的偏差/角度自动转云台
  *   #<id>P<pwm>T<ms>!  套件原指令：id1=Yaw id2=Pitch，pwm 500~2500，例：#001P1500T200!（id 1=Yaw 2=Pitch）
  *   f <0|1|2|3>        发射：0 停 / 1 单发 / 2 连发 / 3 三连发（单键 f 默认三连发）
  *   t <0|1>            遥测开关
  *   m <idx> <spd>      单轮点动标定，例：m 0 300
  *   flip <idx>         翻转该轮方向标定
  *   cal                打印当前标定值（标定完抄回 rm_config.h）
  *   keep <0|1>         失联 500ms 自动停车 开/关（台架调试时关）
  *   p                  心跳/版本
  *   x                  全部停车
  *   h                  帮助
  * 兼容单字符（老的蓝牙APP还能用）：W A S D Q E Z C J K X 1 2 3 F R
  ******************************************************************************
  */

#include "rm_remote.h"
#include "rm_protocol.h"
#include "rm_config.h"
#include "rm_servo.h"
#include "rm_chassis.h"
#if (RM_ENABLE_VISION != 0)
#include "rm_vision.h"
#endif
#if (RM_ENABLE_KBM != 0)
#include "rm_kbm.h"
#endif

#include "usart.h"
#include "main.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ==========================================================================
 * 内部变量
 * ========================================================================== */
static volatile uint8_t  s_rx_buf[RM_UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static uint8_t           s_rx_byte;

static char    s_line[RM_LINE_BUF_SIZE];
static uint8_t s_line_len;

static uint8_t  s_tx_buf[RM_FRAME_OVERHEAD + RM_FRAME_MAX_PAYLOAD + 8u];
static volatile uint8_t s_tx_busy;

static uint32_t s_last_rx_ms;
static uint8_t  s_link_ok;
static uint8_t  s_keepalive = RM_LINK_KEEPALIVE_DEFAULT;

static uint16_t s_frame_cnt;
static uint16_t s_text_cnt;
static uint16_t s_err_cnt;

static rm_cmd_handler_t s_handler;

/* ==========================================================================
 * 环形缓冲
 * ========================================================================== */
static void rx_push(uint8_t b)
{
    uint16_t next = (uint16_t)((s_rx_head + 1u) % RM_UART_RX_BUF_SIZE);

    if (next != s_rx_tail)
    {
        s_rx_buf[s_rx_head] = b;
        s_rx_head = next;
    }
    else
    {
        s_err_cnt++;            /* 缓冲满：丢字节，同时计入错误 */
    }
}

static uint8_t rx_pop(uint8_t *out)
{
    if (s_rx_tail == s_rx_head)
    {
        return 0u;
    }
    *out = s_rx_buf[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) % RM_UART_RX_BUF_SIZE);
    return 1u;
}

/* ==========================================================================
 * HAL 回调
 * ========================================================================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        rx_push(s_rx_byte);
        (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
    }
#if (RM_ENABLE_KBM != 0)
    else if (huart->Instance == USART2)
    {
        /* 键鼠链路走自己的缓冲和状态机，这里只做转发 + 重新挂接收 */
        Kbm_UartRxIsr(huart);
    }
#endif
#if (RM_ENABLE_VISION != 0)
    else if (Vision_IsUart(huart) != 0u)
    {
        /* 视觉模块：它自己管接收缓冲，这里只转发 + 重新挂接收 */
        Vision_RxCplt();
    }
#endif
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        s_tx_busy = 0u;
    }
}

/* ==========================================================================
 * 初始化
 * ========================================================================== */
void Remote_Init(void)
{
    s_rx_head     = 0u;
    s_rx_tail     = 0u;
    s_line_len    = 0u;
    s_tx_busy     = 0u;
    s_last_rx_ms  = HAL_GetTick();
    s_link_ok     = 0u;
    s_frame_cnt   = 0u;
    s_text_cnt    = 0u;
    s_err_cnt     = 0u;
    s_keepalive   = RM_LINK_KEEPALIVE_DEFAULT;

    /* 统一在这里设置波特率，避免 CubeMX 重新生成 usart.c 时被改回去 */
    huart1.Init.BaudRate = RM_UART_BAUD_DEFAULT;
    (void)HAL_UART_Init(&huart1);

    (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
}

/* ==========================================================================
 * 发送
 * ========================================================================== */
void Remote_TxFrame(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint8_t n;

    if (s_tx_busy != 0u)
    {
        return;                 /* 上一帧还没发完，这一帧丢掉（遥测允许丢） */
    }

    n = Proto_BuildFrame(s_tx_buf, cmd, payload, len);
    s_tx_busy = 1u;
    if (HAL_UART_Transmit_IT(&huart1, s_tx_buf, n) != HAL_OK)
    {
        s_tx_busy = 0u;
    }
}

void Remote_TxText(const char *s)
{
    uint16_t n;

    if (s_tx_busy != 0u)
    {
        return;
    }

    n = (uint16_t)strlen(s);
    if (n > 100u)
    {
        n = 100u;
    }
    s_tx_busy = 1u;
    if (HAL_UART_Transmit(&huart1, (uint8_t *)s, n, 200u) != HAL_OK)
    {
        /* 超时就算了 */
    }
    s_tx_busy = 0u;
}

/* ==========================================================================
 * 文本 -> 统一指令
 * ========================================================================== */
static int32_t text_atoi(const char *p, const char **end)
{
    int32_t v = 0;
    int32_t sign = 1;

    while ((*p == ' ') || (*p == '\t'))
    {
        p++;
    }
    if (*p == '-')
    {
        sign = -1;
        p++;
    }
    else if (*p == '+')
    {
        p++;
    }
    while ((*p >= '0') && (*p <= '9'))
    {
        v = v * 10 + (int32_t)(*p - '0');
        p++;
    }
    *end = p;
    return v * sign;
}

/* 把 payload 交给应用层（和帧指令走同一条路） */
static void emit_cmd(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    if (s_handler != 0)
    {
        s_handler(cmd, payload, len);
    }
}

static const char *k_help =
    "RM-CAR v3.3 (f: 1=shot 2=auto 3=burst)\r\n"
#if (RM_ENABLE_REFEREE != 0)
    "v vx vy wz | s deg10 | y deg10 | g | follow 0/1 | aim 0/1 | f 0/1/2/3 | t 0/1 | m idx spd | flip idx\r\n"
    "cal | rf | rftest | kb | keep 0/1 | p | x | h\r\n"
    "#idPpwmTms! (id1=Yaw id2=Pitch)\r\n";
#else
    "v vx vy wz | s deg10 | y deg10 | g | follow 0/1 | aim 0/1 | f 0/1/2/3 | t 0/1 | m idx spd | flip idx\r\n"
    "cal | keep 0/1 | p | x | h\r\n"
    "#idPpwmTms! (id1=Yaw id2=Pitch)\r\n";
#endif

/* 云台状态：串口助手里敲 g 就能看两轴在哪 */
static void report_gimbal(void)
{
    char buf[96];
    int16_t y = Yaw_GetDeg10();
    int16_t p = Servo_GetDeg10();

    (void)sprintf(buf, "gimbal yaw=%d.%d pitch=%d.%d homing=%u follow=%u\r\n",
                  (int)(y / 10), (int)((y % 10) < 0 ? -(y % 10) : (y % 10)),
                  (int)(p / 10), (int)((p % 10) < 0 ? -(p % 10) : (p % 10)),
                  (unsigned)Servo_IsHoming(), (unsigned)Chassis_GetFollow());
    Remote_TxText(buf);
#if (RM_ENABLE_VISION != 0)
    (void)sprintf(buf, "vision valid=%u mode=%u aim=%u dx=%d dy=%d frames=%lu\r\n",
                  (unsigned)Vision_IsValid(), (unsigned)Vision_GetMode(),
                  (unsigned)Vision_GetAim(),
                  (int)Vision_GetDx(), (int)Vision_GetDy(),
                  (unsigned long)Vision_GetFrameCount());
    Remote_TxText(buf);

    /* 最近一帧原文：模块协议不明时靠这一行看出它到底发了什么 */
    {
        const char *src = Vision_GetLastLine();
        char tmp[25];
        uint8_t i;

        for (i = 0u; i < (uint8_t)(sizeof(tmp) - 1u) && src[i] != '\0'; i++)
        {
            char ch = src[i];
            tmp[i] = ((ch >= 0x20) && (ch <= 0x7e)) ? ch : '.';
        }
        tmp[i] = '\0';
        (void)sprintf(buf, "vision raw=%s\r\n", tmp);
        Remote_TxText(buf);
    }
#endif
}

/* 套件原指令 #idPpwmTms!：pwm 直接换回角度 x10，两个 id 对应两轴 */
static void kit_servo_frame(const char *p)
{
    const char *q;
    int32_t id  = text_atoi(p + 1, &q);
    int32_t pwm = -1;
    int32_t ms  = 0;
    int32_t span;
    int32_t deg10;
    uint8_t payload[4];

    if ((*q == 'P') || (*q == 'p')) { pwm = text_atoi(q + 1, &q); }
    if ((*q == 'T') || (*q == 't')) { ms  = text_atoi(q + 1, &q); }
    (void)ms;                        /* 舵机内部插值时间，我们只取目标角 */

    if (pwm < 0)
    {
        Remote_TxText("err: servo frame\r\n");
        return;
    }
    if (pwm < (int32_t)RM_GIMBAL_PULSE_MIN_US) { pwm = (int32_t)RM_GIMBAL_PULSE_MIN_US; }
    if (pwm > (int32_t)RM_GIMBAL_PULSE_MAX_US) { pwm = (int32_t)RM_GIMBAL_PULSE_MAX_US; }

    span  = (int32_t)RM_GIMBAL_PULSE_MAX_US - (int32_t)RM_GIMBAL_PULSE_MIN_US;
    deg10 = ((pwm - (int32_t)RM_GIMBAL_PULSE_MIN_US) * 1800) / span;

    Proto_PutI16(&payload[0], (id == (int32_t)RM_GIMBAL_BUS_YAW_ID) ? (int16_t)deg10 : Yaw_GetTargetDeg10());
    Proto_PutI16(&payload[2], (id == (int32_t)RM_GIMBAL_BUS_YAW_ID) ? Servo_GetTargetDeg10() : (int16_t)deg10);
    emit_cmd(RM_CMD_SERVO, payload, 4u);
}

static void handle_text_line(char *line)
{
    const char *p = line;
    const char *q;
    uint8_t payload[8];

    /* 去首尾空白 */
    while ((*p == ' ') || (*p == '\t'))
    {
        p++;
    }
    if (*p == '\0')
    {
        return;
    }

    s_text_cnt++;

    /* ---------- 兼容单字符指令 ---------- */
    if ((p[0] != '\0') && (p[1] == '\0'))
    {
        int32_t vx = 0;
        int32_t vy = 0;
        int32_t wz = 0;
        uint8_t is_move = 1u;

        switch (p[0])
        {
            case 'w': case 'W': vx =  RM_LEGACY_SPEED; break;
            case 's': case 'S': vx = -RM_LEGACY_SPEED; break;
            case 'a': case 'A': vy =  RM_LEGACY_SPEED; break;
            case 'd': case 'D': vy = -RM_LEGACY_SPEED; break;
            case 'q': case 'Q': vx =  RM_LEGACY_SPEED; vy =  RM_LEGACY_SPEED; break;
            case 'e': case 'E': vx =  RM_LEGACY_SPEED; vy = -RM_LEGACY_SPEED; break;
            case 'z': case 'Z': vx = -RM_LEGACY_SPEED; vy =  RM_LEGACY_SPEED; break;
            case 'c': case 'C': vx = -RM_LEGACY_SPEED; vy = -RM_LEGACY_SPEED; break;
            case 'j': case 'J': wz =  RM_LEGACY_SPEED; break;
            case 'k': case 'K': wz = -RM_LEGACY_SPEED; break;

            case 'x': case 'X':
                emit_cmd(RM_CMD_STOP, 0, 0u);
                return;

            case 'g': case 'G':
                report_gimbal();
                return;

            case '1':
                Proto_PutI16(&payload[0], 300);   /* 30.0° */
                emit_cmd(RM_CMD_SERVO, payload, 2u);
                return;
            case '2':
                Proto_PutI16(&payload[0], 450);
                emit_cmd(RM_CMD_SERVO, payload, 2u);
                return;
            case '3':
                Proto_PutI16(&payload[0], 600);
                emit_cmd(RM_CMD_SERVO, payload, 2u);
                return;

            case 'f': case 'F':
                payload[0] = 3u;            /* 单键 = 三连发（AEG 单发会卡半压缩位） */
                emit_cmd(RM_CMD_FIRE, payload, 1u);
                return;
            case 'r': case 'R':
                payload[0] = 0u;
                emit_cmd(RM_CMD_FIRE, payload, 1u);
                return;

            default:
                is_move = 0u;
                break;
        }

        if (is_move != 0u)
        {
            Proto_PutI16(&payload[0], (int16_t)vx);
            Proto_PutI16(&payload[2], (int16_t)vy);
            Proto_PutI16(&payload[4], (int16_t)wz);
            emit_cmd(RM_CMD_VEL, payload, 6u);
        }
        else
        {
            Remote_TxText("err: unknown key\r\n");
        }
        return;
    }

    /* ---------- 文本指令 ---------- */
    if (p[0] == '#')
    {
        kit_servo_frame(p);
    }
    else if ((p[0] == 'y') || (p[0] == 'Y'))
    {
        int32_t deg10 = text_atoi(p + 1, &q);
        Proto_PutI16(&payload[0], (int16_t)deg10);
        Proto_PutI16(&payload[2], Servo_GetTargetDeg10());
        emit_cmd(RM_CMD_SERVO, payload, 4u);
    }
    else if ((p[0] == 'v') || (p[0] == 'V'))
    {
        int32_t vx;
        int32_t vy;
        int32_t wz;

        vx = text_atoi(p + 1, &q); vy = text_atoi(q, &q); wz = text_atoi(q, &q);
        Proto_PutI16(&payload[0], (int16_t)vx);
        Proto_PutI16(&payload[2], (int16_t)vy);
        Proto_PutI16(&payload[4], (int16_t)wz);
        emit_cmd(RM_CMD_VEL, payload, 6u);
    }
    else if (p[0] == 's')
    {
        int32_t deg10 = text_atoi(p + 1, &q);
        Proto_PutI16(&payload[0], (int16_t)deg10);
        emit_cmd(RM_CMD_SERVO, payload, 2u);
    }
    else if ((p[0] == 'i') && (p[1] == 'd'))
    {
        int32_t id = text_atoi(p + 2, &q);
        char buf[20];

        if ((id < 0) || (id > 254))
        {
            Remote_TxText("id 0..254\r\n");
        }
        else
        {
            /* 广播改 ID：同一根线上只接一个舵机时用，改完再接第二个 */
            (void)sprintf(buf, "#255PID%03u!", (unsigned)id);
            Gimbal_BusRaw(buf);
            Remote_TxText("busid sent\r\n");
        }
    }
    else if (strncmp(p, "follow", 6) == 0)
    {
        int32_t on;

        on = text_atoi(p + 6, &q);
        Chassis_SetFollow((on != 0) ? 1u : 0u);
        Remote_TxText((Chassis_GetFollow() != 0u) ? "follow on\r\n" : "follow off\r\n");
    }
    #if (RM_ENABLE_VISION != 0)
    else if (strncmp(p, "aim", 3) == 0)
    {
        int32_t on;

        on = text_atoi(p + 3, &q);
        Vision_SetAim((on != 0) ? 1u : 0u);
        Remote_TxText((Vision_GetAim() != 0u) ? "aim on\r\n" : "aim off\r\n");
    }
#endif
    else if (p[0] == 'f')
    {
        int32_t mode = text_atoi(p + 1, &q);
        payload[0] = (uint8_t)mode;
        emit_cmd(RM_CMD_FIRE, payload, 1u);
    }
    else if (p[0] == 't')
    {
        int32_t on = text_atoi(p + 1, &q);
        payload[0] = (uint8_t)on;
        emit_cmd(RM_CMD_TELEM, payload, 1u);
    }
    else if (p[0] == 'm')
    {
        int32_t idx = text_atoi(p + 1, &q);
        int32_t spd = text_atoi(q, &q);
        payload[0] = (uint8_t)idx;
        Proto_PutI16(&payload[1], (int16_t)spd);
        emit_cmd(RM_CMD_WHEELTEST, payload, 3u);
    }
    else if ((p[0] == 'k') && (p[1] == 'e'))
    {
        int32_t on = (strstr(p, "0") != 0) ? 0 : 1;
        s_keepalive = (uint8_t)on;
        Remote_TxText(on ? "keepalive on\r\n" : "keepalive off\r\n");
    }
    else if ((p[0] == 'c') && (p[1] == 'a'))
    {
        /* 标定回显：模式/方向/trim/限幅由应用层打印 */
        emit_cmd(RM_CMD_CAL, 0, 0u);
    }
    #if (RM_ENABLE_REFEREE != 0)
    else if ((p[0] == 'r') && (p[1] == 'f'))
    {
        /* rf = 裁判系统当前状态；rftest = 内置自检（不接硬件也能验证解析） */
        payload[0] = (uint8_t)((p[2] == 't') ? 't' : 'r');
        emit_cmd(RM_CMD_REF, payload, 1u);
    }
#endif
    #if (RM_ENABLE_KBM != 0)
    else if ((p[0] == 'k') && (p[1] == 'b'))
    {
        /* kb = 打印键鼠链路状态（按键、鼠标位移、帧数、错误数） */
        emit_cmd(RM_CMD_KBM, 0, 0u);
    }
#endif
    else if ((p[0] == 'p') || (p[0] == 'P'))
    {
        emit_cmd(RM_CMD_PING, 0, 0u);
    }
    else if ((p[0] == 'x') || (p[0] == 'X'))
    {
        emit_cmd(RM_CMD_STOP, 0, 0u);
    }
    else if ((p[0] == 'h') || (p[0] == 'H'))
    {
        Remote_TxText(k_help);
    }
    else
    {
        Remote_TxText("err: unknown cmd\r\n");
    }
}

static void handle_line_byte(uint8_t b, uint32_t now_ms)
{
    if ((b == '\r') || (b == '\n'))
    {
        if (s_line_len > 0u)
        {
            s_line[s_line_len] = '\0';
            s_line_len = 0u;
            s_link_ok = 1u;
            s_last_rx_ms = now_ms;
            handle_text_line(s_line);
        }
        return;
    }

    if ((b < 0x20u) || (b > 0x7Eu))
    {
        return;                 /* 非可见字符直接丢 */
    }

    if (s_line_len < (RM_LINE_BUF_SIZE - 1u))
    {
        s_line[s_line_len] = (char)b;
        s_line_len++;
    }
    else
    {
        s_line_len = 0u;        /* 超长行：丢弃 */
        s_err_cnt++;
    }
}

/* ==========================================================================
 * 任务
 * ========================================================================== */
void Remote_Task(void)
{
    uint8_t b;
    uint8_t n = 0u;
    uint32_t now = HAL_GetTick();

    /* 一次最多处理 32 字节，避免占满节拍。
     * 分流规则：先问帧解析器"这个字节是不是你的"，它不要才当文本处理。 */
    while ((n < 32u) && (rx_pop(&b) != 0u))
    {
        n++;
        if (Proto_ParseByte(b, now) != 0u)
        {
            continue;
        }
        handle_line_byte(b, now);
    }
}

/* ==========================================================================
 * 查询
 * ========================================================================== */
void Remote_SetHandler(rm_cmd_handler_t handler)
{
    s_handler = handler;
}

void Remote_LinkTimerReset(void)
{
    s_last_rx_ms = HAL_GetTick();
    s_link_ok = 1u;
}

uint8_t Remote_LinkOk(void)
{
    return s_link_ok;
}

uint32_t Remote_GetLastRxMs(void)
{
    return s_last_rx_ms;
}

void Remote_SetKeepAlive(uint8_t on)
{
    s_keepalive = (on != 0u) ? 1u : 0u;
}

uint8_t Remote_GetKeepAlive(void)
{
    return s_keepalive;
}

uint16_t Remote_GetFrameCount(void)
{
    return s_frame_cnt;
}

uint16_t Remote_GetTextCount(void)
{
    return s_text_cnt;
}

uint16_t Remote_GetErrorCount(void)
{
    return s_err_cnt;
}

