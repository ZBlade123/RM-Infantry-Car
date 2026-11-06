#include "app_ps2.h"

/* 初始指令，如果没有从上位机下载指令的话会执行下面的指令 */
const char *pre_cmd_set_grn[PSX_BUTTON_NUM] = {
	// 绿灯模式下按键的配置
	"<G_L2:#005P0600T2000!^#005PDST!>", // L2  左上500
	"<G_R2:#005P2400T2000!^#005PDST!>", // R2	右上500
	"<G_L1:#004P0600T2000!^#004PDST!>", // L1	左上1000
	"<G_R1:#004P2400T2000!^#004PDST!>", // R1	右上1000
	"<G_RU:#002P2400T2000!^#002PDST!>", // RU	前进1000
	"<G_RR:#003P2400T2000!^#003PDST!>", // RR	右平移1000
	"<G_RD:#002P0600T2000!^#002PDST!>", // RD	后退1000
	"<G_RL:#003P0600T2000!^#003PDST!>", // RL	左平移1000
	"<G_SE:#255P1500T2000!>",			// SE
	"<G_AL:>",							// AL
	"<G_AR:>",							// AR
	"<G_ST:#255P1500T2000!>",			// ST
	"<G_LU:#001P0600T2000!^#001PDST!>", // LU	前进500
	"<G_LR:#000P0600T2000!^#000PDST!>", // LR	右转500
	"<G_LD:#001P2400T2000!^#001PDST!>", // LD	后退500
	"<G_LL:#000P2400T2000!^#000PDST!>", // LL	左转500
};

static u16 ps2_cmd = 0;
static u16 ps2_cmd_last = 0;
static u16 ps2_status_flag = 0xffff;
void parse_psx_buf(unsigned char *buf, unsigned char mode);
void loop_ps2_car(void);

/**
 * @函数描述: PS2设备控制初始化
 * @return {*}
 */
void app_ps2_init(void)
{
	usb_ps2_Init(); /* PS2引脚初始化 */
}

/**
 * @函数描述: 循环执行工作
 * @return {*}
 */
void app_ps2_run(void)
{
	static u32 systick_ms_bak = 0;
	uint16_t pos;
	// 每50ms处理1次
	if (millis() - systick_ms_bak < 20)
		return;
	systick_ms_bak = millis();
	ps2_write_read(); /* 读取ps2数据 */
	if (ps2_buf[0] == 0x01 && ps2_buf[8] == 0x05)
	{
		//    if (ps2_buf[6] & 0x01) /* L2 */
		//        ps2_cmd &= ~0X0001;
		//    else
		//        ps2_cmd |= 0X0001;
		if (ps2_buf[6] & 0x04) /* L2 */
			ps2_cmd &= ~0X0001;
		else
			ps2_cmd |= 0X0001;

		if (ps2_buf[6] & 0x08) /* R2 */
			ps2_cmd &= ~0X0002;
		else
			ps2_cmd |= 0X0002;

		if (ps2_buf[6] & 0x01) /* L1 */
			ps2_cmd &= ~0X0004;
		else
			ps2_cmd |= 0X0004;

		if (ps2_buf[6] & 0x02) /* R1 */
			ps2_cmd &= ~0X0008;
		else
			ps2_cmd |= 0X0008;

		if (ps2_buf[5] & 0x10) /* RU */
			ps2_cmd &= ~0X0010;
		else
			ps2_cmd |= 0X0010;

		if (ps2_buf[5] & 0x20) /* RR */
			ps2_cmd &= ~0X0020;
		else
			ps2_cmd |= 0X0020;

		if (ps2_buf[5] & 0x40) /* RD */
			ps2_cmd &= ~0X0040;
		else
			ps2_cmd |= 0X0040;

		if (ps2_buf[5] & 0x80) /* RL */
			ps2_cmd &= ~0X0080;
		else
			ps2_cmd |= 0X0080;

		if (ps2_buf[5] == 0x00) /* LU */
			ps2_cmd &= ~0X1000;
		else
			ps2_cmd |= 0X1000;

		if (ps2_buf[5] == 0x02) /* LR */
			ps2_cmd &= ~0X2000;
		else
			ps2_cmd |= 0X2000;

		if (ps2_buf[5] == 0x04) /* LU */
			ps2_cmd &= ~0X4000;
		else
			ps2_cmd |= 0X4000;

		if (ps2_buf[5] == 0x06) /* LL */
			ps2_cmd &= ~0X8000;
		else
			ps2_cmd |= 0X8000;

		if (ps2_buf[6] & 0x10) /* SE */
			ps2_cmd &= ~0X0100;
		else
			ps2_cmd |= 0X0100;

		if ((ps2_buf[6] & 0x40)) /* AL */
			ps2_cmd &= ~0X0200;
		else
			ps2_cmd |= 0X0200;

		if ((ps2_buf[6] & 0x80)) /* AR */
			ps2_cmd &= ~0X0400;
		else
			ps2_cmd |= 0X0400;

		if (ps2_buf[6] & 0x20) /* SE */
			ps2_cmd &= ~0X0800;
		else
			ps2_cmd |= 0X0800;

		if (ps2_cmd != ps2_cmd_last)
		{
			for (u8 i = 0; i < 16; i++)
			{
				if (!(ps2_cmd & (1 << i))) /* 当前手柄按下 */
				{
					if ((ps2_status_flag & (1 << i))) /* 上一次手柄未按下 */
					{
						memset(uart_receive_buf, 0, sizeof(uart_receive_buf));

						memcpy((char *)uart_receive_buf, (char *)pre_cmd_set_grn[i], strlen(pre_cmd_set_grn[i]));

						pos = str_contain_str(uart_receive_buf, "^");
						if (pos)
							uart_receive_buf[pos - 1] = '\0';

						strcpy((char *)cmd_return, (char *)uart_receive_buf + 6);
						/* 判断指令格式 */
						if (str_contain_str(cmd_return, "$"))
						{
							parse_cmd(cmd_return);
						}
						else if (str_contain_str(cmd_return, "#"))
						{
							parse_action(cmd_return);
						}
					}
				}
				else
				{
					if (!(ps2_status_flag & (1 << i)))
					{
						memset(uart_receive_buf, 0, sizeof(uart_receive_buf));

						memcpy((char *)uart_receive_buf, (char *)pre_cmd_set_grn[i], strlen(pre_cmd_set_grn[i]));

						pos = str_contain_str(uart_receive_buf, "^");
						if (pos)
						{
							strcpy((char *)cmd_return, (char *)uart_receive_buf + pos);
							/* 判断指令格式 */
							if (str_contain_str(cmd_return, "$"))
							{
								parse_cmd(cmd_return);
							}
							else if (str_contain_str(cmd_return, "#"))
							{
								parse_action(cmd_return);
							}
						}
					}
				}
			}
			ps2_status_flag = ps2_cmd;
		}

		/* 推动遥感值操作
			上右值； 0-127
			左下值； 255-128
			一般是手柄用来控制底盘车，若无底盘车请忽略以下代码
		*/
		float car_left1 = 0, car_right1 = 0, car_left2 = 0, car_right2 = 0;
		static float car_left1_bak = 0, car_right1_bak = 0, car_left2_bak = 0, car_right2_bak = 0;
		/* 前进后退 */
		// printf("L_UD= %d   ", ps2_buf[1]);
		if (abs(128 - ps2_buf[4]) > 20) /* 左边上下 */
		{
			car_left1 = car_left1 + (0x7f - ps2_buf[4]) * 2;
			car_right1 = car_right1 + (0x7f - ps2_buf[4]) * 2;
			car_left2 = car_left2 + (0x7f - ps2_buf[4]) * 2;
			car_right2 = car_right2 + (0x7f - ps2_buf[4]) * 2;
		}
		// printf("R_UD= %d   ", ps2_buf[3]);
		if (abs(128 - ps2_buf[2]) > 20) /* 右边上下 */
		{
			car_left1 = car_left1 + (0x7f - ps2_buf[2]) * 2;
			car_right1 = car_right1 + (0x7f - ps2_buf[2]) * 2;
			car_left2 = car_left2 + (0x7f - ps2_buf[2]) * 2;
			car_right2 = car_right2 + (0x7f - ps2_buf[2]) * 2;
		}

		/* 左边左右, */
		// printf("L_RL= %d   ", ps2_buf[0]);
		if (abs(128 - ps2_buf[3]) > 20)
		{
			car_left1 = car_left1 - (0x7f - ps2_buf[3]) * 2;
			car_right1 = car_right1 + (0x7f - ps2_buf[3]) * 2;
			car_left2 = car_left2 + (0x7f - ps2_buf[3]) * 2;
			car_right2 = car_right2 - (0x7f - ps2_buf[3]) * 2;
		}
		/* 右边左右，转弯 */
		// printf("R_RL= %d\r\n", ps2_buf[2]);
		if (abs(128 - ps2_buf[1]) > 20)
		{
			car_left1 = car_left1 - (0x7f - ps2_buf[1]) * 2;
			car_right1 = car_right1 + (0x7f - ps2_buf[1]) * 2;
			car_left2 = car_left2 - (0x7f - ps2_buf[1]) * 2;
			car_right2 = car_right2 + (0x7f - ps2_buf[1]) * 2;
		}
		if ((car_left1_bak != car_left1) || (car_right1_bak != car_right1) || (car_left2_bak != car_left2) || (car_right2_bak != car_right2))
		{
			car_left1_bak = car_left1;
			car_right1_bak = car_right1;

			car_left2_bak = car_left2;
			car_right2_bak = car_right2;
			//			motor_speed_set(car_left1 / 1000, car_right1 / 1000, car_left2 / 1000, car_right2 / 1000);
		}
	}
}
