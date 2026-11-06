/**
  ******************************************************************************
  * @file    rm_referee.c
  * @brief   RoboMaster 裁判系统协议解析实现
  *
  * 帧结构（官方手册）：
  *   | SOF(0xA5) | data_len(2, 小端) | seq(1) | CRC8(1) | cmd_id(2) | data(data_len) | CRC16(2) |
  *   CRC8  初值 0xFF，多项式 0x31，计算范围 = 前 4 字节
  *   CRC16 初值 0xFFFF，多项式 0x1021，计算范围 = cmd_id + data
  *
  * 字段偏移说明：
  *   0x0201 里"热量上限"和"冷却值"两个 u16 的先后顺序在不同赛季手册里有出入，
  *   这里不写死顺序，而是取较大者为上限、较小者为冷却（上限 60+、冷却 10~40，不会混淆）。
  ******************************************************************************
  */

#include "rm_referee.h"
#include "rm_crc.h"
#include <string.h>

/* ==========================================================================
 * 解析状态机
 * ========================================================================== */
#define REF_ST_SOF      0u
#define REF_ST_LEN_LO   1u
#define REF_ST_LEN_HI   2u
#define REF_ST_SEQ      3u
#define REF_ST_CRC8     4u
#define REF_ST_DATA     5u

static struct
{
    uint8_t  buf[RM_REFEREE_FRAME_MAX];
    uint16_t data_len;
    uint16_t index;      /* 已写入 buf 的字节数 */
    uint8_t  step;
    uint8_t  seq_tx;     /* 发送序号 */
} s_parser;

static rm_referee_status_t s_status;

/* ==========================================================================
 * 小工具
 * ========================================================================== */
static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static float rd_float(const uint8_t *p)
{
    union
    {
        uint32_t u;
        float    f;
    } v;

    v.u = (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
    return v.f;
}

static void wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void parser_reset(void)
{
    s_parser.data_len = 0u;
    s_parser.index    = 0u;
    s_parser.step     = REF_ST_SOF;
}

/* ==========================================================================
 * 帧分发
 * ========================================================================== */
static void dispatch_frame(uint16_t cmd, const uint8_t *pl, uint16_t len, uint32_t now_ms)
{
    uint16_t i;
    uint16_t a;
    uint16_t b;

    s_status.last_cmd  = cmd;
    s_status.last_rx_ms = now_ms;
    s_status.online    = 1u;
    s_status.frame_count++;

    /* 原始数据段：不管认不认识这条命令，都留一份给调试用 */
    s_status.raw_cmd = cmd;
    s_status.raw_len = (len > RM_REFEREE_PAYLOAD_MAX) ? RM_REFEREE_PAYLOAD_MAX : (uint8_t)len;
    for (i = 0u; i < (uint16_t)s_status.raw_len; i++)
    {
        s_status.raw[i] = pl[i];
    }

    switch (cmd)
    {
        case RM_REF_CMD_GAME_STATUS:
            if (len >= 3u)
            {
                s_status.game_type     = (uint8_t)(pl[0] & 0x0Fu);
                s_status.game_progress = (uint8_t)((pl[0] >> 4) & 0x0Fu);
                s_status.stage_remain_s = rd_u16(&pl[1]);
                s_status.valid_mask |= RM_REFEREE_VALID_GAME;
            }
            else
            {
                s_status.len_error_count++;
            }
            break;

        case RM_REF_CMD_GAME_ROBOT_HP:
            if (len >= 32u)
            {
                for (i = 0u; i < 16u; i++)
                {
                    s_status.robot_hp[i] = rd_u16(&pl[i * 2u]);
                }
                s_status.valid_mask |= RM_REFEREE_VALID_HP;
            }
            else
            {
                s_status.len_error_count++;
            }
            break;

        case RM_REF_CMD_ROBOT_STATUS:
            if (len >= 13u)
            {
                s_status.robot_id      = pl[0];
                s_status.robot_level   = pl[1];
                s_status.hp            = rd_u16(&pl[2]);
                s_status.max_hp        = rd_u16(&pl[4]);
                a                      = rd_u16(&pl[6]);
                b                      = rd_u16(&pl[8]);
                s_status.heat_limit    = (a > b) ? a : b;
                s_status.cooling_rate  = (a > b) ? b : a;
                s_status.chassis_power_limit = rd_u16(&pl[10]);
                s_status.power_out_bits = pl[12];
                s_status.valid_mask |= RM_REFEREE_VALID_ROBOT;
            }
            else
            {
                s_status.len_error_count++;
            }
            break;

        case RM_REF_CMD_POWER_HEAT:
            if (len >= 16u)
            {
                s_status.buffer_energy = rd_u16(&pl[8]);
                s_status.heat_17mm_1   = rd_u16(&pl[10]);
                s_status.heat_17mm_2   = rd_u16(&pl[12]);
                s_status.heat_42mm     = rd_u16(&pl[14]);
                s_status.valid_mask |= RM_REFEREE_VALID_POWER;
            }
            else
            {
                s_status.len_error_count++;
            }
            break;

        case RM_REF_CMD_SHOOT_DATA:
            if (len >= 7u)
            {
                s_status.bullet_type    = pl[0];
                s_status.shooter_number = pl[1];
                s_status.launching_freq = pl[2];
                s_status.initial_speed  = rd_float(&pl[3]);
                s_status.valid_mask |= RM_REFEREE_VALID_SHOOT;
            }
            else
            {
                s_status.len_error_count++;
            }
            break;

        case RM_REF_CMD_PROJECTILE:
            if (len >= 8u)
            {
                s_status.allow_17mm    = rd_u16(&pl[0]);
                s_status.allow_42mm    = rd_u16(&pl[2]);
                s_status.gold_coin     = rd_u16(&pl[4]);
                s_status.allow_fortress = rd_u16(&pl[6]);
                s_status.valid_mask |= RM_REFEREE_VALID_ALLOW;
            }
            else
            {
                s_status.len_error_count++;
            }
            break;

        default:
            /* 0x0301 交互数据 / 0x0304 自定义控制器(键鼠) 等：先留原始数据，等核对手册再解码 */
            s_status.valid_mask |= RM_REFEREE_VALID_RAW;
            break;
    }
}

/* ==========================================================================
 * 逐字节解析
 * ========================================================================== */
static void parse_byte(uint8_t byte, uint32_t now_ms)
{
    switch (s_parser.step)
    {
        case REF_ST_SOF:
            if (byte == RM_REFEREE_SOF)
            {
                s_parser.buf[0]  = byte;
                s_parser.index   = 1u;
                s_parser.step    = REF_ST_LEN_LO;
            }
            break;

        case REF_ST_LEN_LO:
            s_parser.buf[s_parser.index++] = byte;
            s_parser.step = REF_ST_LEN_HI;
            break;

        case REF_ST_LEN_HI:
            s_parser.buf[s_parser.index++] = byte;
            s_parser.data_len = (uint16_t)((uint16_t)s_parser.buf[1] |
                                           ((uint16_t)s_parser.buf[2] << 8));
            if (s_parser.data_len > (uint16_t)(RM_REFEREE_FRAME_MAX - 9u))
            {
                /* 长度非法：直接从本字节之后重新找帧头 */
                s_status.len_error_count++;
                parser_reset();
            }
            else
            {
                s_parser.step = REF_ST_SEQ;
            }
            break;

        case REF_ST_SEQ:
            s_parser.buf[s_parser.index++] = byte;
            s_parser.step = REF_ST_CRC8;
            break;

        case REF_ST_CRC8:
            s_parser.buf[s_parser.index++] = byte;
            if (RM_Crc8(RM_REFEREE_CRC8_INIT, s_parser.buf, 4u) != byte)
            {
                s_status.crc_error_count++;
                parser_reset();
            }
            else
            {
                /* 帧头通过，接着收 cmd_id + data + crc16 */
                s_parser.step = REF_ST_DATA;
            }
            break;

        case REF_ST_DATA:
            if (s_parser.index < RM_REFEREE_FRAME_MAX)
            {
                s_parser.buf[s_parser.index++] = byte;
            }
            else
            {
                /* 溢出保护：这条帧丢了 */
                s_status.overflow_count++;
                parser_reset();
                break;
            }
            if (s_parser.index >= (uint16_t)(s_parser.data_len + 9u))
            {
                uint16_t cmd;
                uint16_t crc_calc;
                uint16_t crc_get;
                uint16_t plen;

                cmd = rd_u16(&s_parser.buf[5]);
                plen = (uint16_t)(s_parser.data_len);
                crc_calc = RM_Crc16(RM_REFEREE_CRC16_INIT, &s_parser.buf[5], (uint16_t)(plen + 2u));
                crc_get  = rd_u16(&s_parser.buf[7u + plen]);

                if (crc_calc == crc_get)
                {
                    dispatch_frame(cmd, &s_parser.buf[7], plen, now_ms);
                }
                else
                {
                    /* 官方手册各赛季对 CRC16 字节序的写法有过出入（小端/大端）。
                     * 这里两种都认：只要命中任意一种就接受，并记录是哪一种，
                     * 这样换赛季/换裁判系统版本都不用改代码。 */
                    uint16_t swapped = (uint16_t)((uint16_t)((crc_get >> 8) & 0xFFu) |
                                                  (uint16_t)((crc_get & 0xFFu) << 8));
                    if (crc_calc == swapped)
                    {
                        s_status.frame_be = 1u;
                        dispatch_frame(cmd, &s_parser.buf[7], plen, now_ms);
                    }
                    else
                    {
                        s_status.crc_error_count++;
                    }
                }
                parser_reset();
            }
            break;

        default:
            parser_reset();
            break;
    }
}

/* ==========================================================================
 * 对外接口
 * ========================================================================== */
void Referee_Reset(void)
{
    /* 全清：解析状态 + 业务数据 + 诊断计数
     * 之所以连 frame_count/crc_error_count 也清，是为了让 "rftest" 自检时
     * 读到的是自检自己的统计，而不是上电以来累计值。 */
    parser_reset();
    (void)memset(&s_status, 0, sizeof(s_status));
}

void Referee_Init(void)
{
    s_parser.seq_tx = 0u;
    Referee_Reset();
}

void Referee_Feed(const uint8_t *data, uint16_t len, uint32_t now_ms)
{
    uint16_t i;

    if (data == 0)
    {
        return;
    }

    for (i = 0u; i < len; i++)
    {
        parse_byte(data[i], now_ms);
    }
}

const rm_referee_status_t *Referee_Get(void)
{
    return &s_status;
}

uint8_t Referee_IsOnline(uint32_t now_ms)
{
    if (s_status.online == 0u)
    {
        return 0u;
    }
    if ((uint32_t)(now_ms - s_status.last_rx_ms) > (uint32_t)RM_REFEREE_TIMEOUT_MS)
    {
        return 0u;
    }
    return 1u;
}

uint16_t Referee_BuildFrame(uint8_t *out,
                            uint16_t cmd_id,
                            const uint8_t *payload,
                            uint16_t payload_len)
{
    uint16_t total;

    if ((out == 0) || (payload_len > (uint16_t)(RM_REFEREE_FRAME_MAX - 9u)))
    {
        return 0u;
    }

    out[0] = RM_REFEREE_SOF;
    wr_u16(&out[1], payload_len);
    out[3] = s_parser.seq_tx++;
    out[4] = RM_Crc8(RM_REFEREE_CRC8_INIT, out, 4u);
    wr_u16(&out[5], cmd_id);
    if ((payload != 0) && (payload_len > 0u))
    {
        (void)memcpy(&out[7], payload, payload_len);
    }
    wr_u16(&out[7u + payload_len],
           RM_Crc16(RM_REFEREE_CRC16_INIT, &out[5], (uint16_t)(payload_len + 2u)));

    total = (uint16_t)(payload_len + 9u);
    return total;
}

uint8_t Referee_SelfTest(uint32_t now_ms)
{
    uint8_t  frame[64];
    uint8_t  pl[13];
    uint16_t n;
    const rm_referee_status_t *st;

    Referee_Reset();

    /* --- 0x0201 本机器人状态：id=7 等级3 血量200/200 上限60 冷却10 功率60 --- */
    pl[0]  = 7u;
    pl[1]  = 3u;
    wr_u16(&pl[2], 200u);
    wr_u16(&pl[4], 200u);
    wr_u16(&pl[6], 10u);    /* 故意把"冷却值"放前面，验证自动识别 */
    wr_u16(&pl[8], 60u);
    wr_u16(&pl[10], 60u);
    pl[12] = 0x07u;

    n = Referee_BuildFrame(frame, RM_REF_CMD_ROBOT_STATUS, pl, 13u);
    Referee_Feed(frame, n, now_ms);
    st = Referee_Get();
    if ((st->robot_id != 7u) || (st->hp != 200u) ||
        (st->heat_limit != 60u) || (st->cooling_rate != 10u))
    {
        return 0u;
    }

    /* --- 0x0001 比赛状态：game_type=1, game_progress=4, 剩余 180s --- */
    pl[0] = (uint8_t)((4u << 4) | 1u);
    wr_u16(&pl[1], 180u);

    n = Referee_BuildFrame(frame, RM_REF_CMD_GAME_STATUS, pl, 3u);
    Referee_Feed(frame, n, now_ms);
    st = Referee_Get();
    if ((st->game_type != 1u) || (st->game_progress != 4u) || (st->stage_remain_s != 180u))
    {
        return 0u;
    }

    /* --- 故意塞一帧坏 CRC，确认被丢掉 --- */
    n = Referee_BuildFrame(frame, RM_REF_CMD_PROJECTILE, pl, 8u);
    frame[7] ^= 0xFFu;
    Referee_Feed(frame, n, now_ms);
    st = Referee_Get();
    if ((st->allow_17mm != 0u) || (st->crc_error_count == 0u))
    {
        return 0u;
    }

    /* --- 帧计数与在线判断 --- */
    if (st->frame_count != 2u)
    {
        return 0u;
    }
    if (Referee_IsOnline(now_ms) == 0u)
    {
        return 0u;
    }

    return 1u;
}
