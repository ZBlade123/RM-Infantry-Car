#include "LTR381/LTR381.h"
#include "soft_i2c/y_soft_i2c.h"

LTR381_HandleTypeDef ltr381;

/* 超时回调函数 */
static uint32_t I2C_TIMEOUT_UserCallback(uint8_t errorCode)
{
	i2c_stop(); // 超时后发送停止信号，释放总线
	printf("LTR381 iic errorCode = %d\r\n", errorCode);
	return 0;
}

/**
 * @brief  软件I2C读寄存器（底层核心函数）
 * @param  hltr: 传感器句柄
 * @param  reg: 寄存器地址
 * @param  value: 读取到的值（输出）
 * @retval 状态：1/ERROR/TIMEOUT
 */
static uint8_t LTR381_ReadReg(uint8_t reg)
{
	uint8_t value = 0;

	// 1. 发送开始信号 + 写地址（先告诉传感器要读的寄存器）
	i2c_start();
	i2c_write_byte(LTR381_I2C_ADDR << 1 | 0); // 写地址（最低位0）
	if (i2c_wait_ack())
	{
		i2c_stop();
		I2C_TIMEOUT_UserCallback(1);
		return 0;
	}

	// 2. 发送寄存器地址
	i2c_write_byte(reg);
	if (i2c_wait_ack())
	{
		i2c_stop();
		I2C_TIMEOUT_UserCallback(2);
		return 0;
	}

	// 3. 发送重复开始信号 + 读地址（切换为读模式）
	i2c_start();
	i2c_write_byte(LTR381_I2C_ADDR << 1 | 1); // 读地址（最低位1）
	if (i2c_wait_ack())
	{
		i2c_stop();
		I2C_TIMEOUT_UserCallback(3);
		return 0;
	}

	// 4. 读取数据（读1字节，最后发送NACK）
	value = i2c_read_byte(0); // 0：读取后发送NACK
	i2c_stop();

	return value;
}

/**
 * @brief  软件I2C写寄存器（底层核心函数）
 * @param  hltr: 传感器句柄
 * @param  reg: 寄存器地址
 * @param  value: 要写入的值
 * @retval 状态：1/ERROR/TIMEOUT
 */
static uint8_t LTR381_WriteReg(uint8_t reg, uint8_t value)
{
	// 1. 发送I2C开始信号
	i2c_start();

	// 2. 发送设备写地址（8位：7位地址+0（写））
	i2c_write_byte(LTR381_I2C_ADDR << 1 | 0); // LTR381_I2C_ADDR已左移1位，最低位为0（写）
	if (i2c_wait_ack())
	{
		i2c_stop();
		I2C_TIMEOUT_UserCallback(4);
		return 0;
	}

	// 3. 发送寄存器地址
	i2c_write_byte(reg);
	if (i2c_wait_ack())
	{
		i2c_stop();
		I2C_TIMEOUT_UserCallback(5);
		return 0;
	}

	// 4. 发送要写入的值
	i2c_write_byte(value);
	if (i2c_wait_ack())
	{
		i2c_stop();
		I2C_TIMEOUT_UserCallback(6);
		return 0;
	}

	// 5. 发送停止信号
	i2c_stop();
	return 1;
}

/**
 * @brief  软件I2C读多字节（用于ALS/RGB数据读取）
 * @param  hltr: 传感器句柄
 * @param  reg: 起始寄存器地址
 * @param  buf: 接收缓冲区
 * @param  len: 要读取的字节数
 * @retval 状态：1/ERROR/TIMEOUT
 */
static uint8_t LTR381_ReadMultiReg(uint8_t reg, uint8_t *buf, uint8_t len)
{
	if (len == 0 || buf == NULL)
		return 0;

	uint8_t i;

	// 1. 发送开始 + 写地址 + 寄存器地址（同读单字节前半段）
	i2c_start();
	i2c_write_byte(LTR381_I2C_ADDR << 1 | 0);
	if (i2c_wait_ack())
	{
		i2c_stop();
		I2C_TIMEOUT_UserCallback(7);
		return 0;
	}

	i2c_write_byte(reg);
	if (i2c_wait_ack())
	{
		i2c_stop();
		I2C_TIMEOUT_UserCallback(8);
		return 0;
	}

	// 2. 重复开始 + 读地址
	i2c_start();
	i2c_write_byte(LTR381_I2C_ADDR << 1 | 1);
	if (i2c_wait_ack())
	{
		i2c_stop();
		I2C_TIMEOUT_UserCallback(9);
		return 0;
	}

	// 3. 读取多字节：前n-1字节读后发ACK，最后1字节发NACK
	for (i = 0; i < len; i++)
	{
		if (i == len - 1)
		{
			buf[i] = i2c_read_byte(0); // 最后1字节：NACK
		}
		else
		{
			buf[i] = i2c_read_byte(1); // 前n-1字节：ACK
		}
	}

	i2c_stop();
	return 1;
}

// 配置传感器：模式，增益，分辨率
uint8_t LTR381_Config(LTR381_HandleTypeDef *hltr, uint8_t mode, uint8_t gain, uint8_t resolution)
{
	// 设置工作模式
	if (LTR381_WriteReg(LTR381_MAIN_CTRL, mode) != 1)
	{
		return 0;
	}

	// 设置增益和分辨率（寄存器位分配需与datasheet匹配）
	uint8_t gain_reg_val = (resolution & 0x70) | (gain & 0x07);
	if (LTR381_WriteReg(LTR381_GAIN, gain_reg_val) != 1)
	{
		return 0;
	}

	// 设置测量速率
	if (LTR381_WriteReg(LTR381_MEAS_RATE, resolution) != 1)
	{
		return 0;
	}

	hltr->mode = mode;
	hltr->gain = gain;
	hltr->resolution = resolution;

	
	return 1;
}

/**
 * @brief  读取ALS+IR环境光数据（复用0x0D~0x0F，此时为环境光数据）
 * @param  hltr: 传感器句柄
 * @param  als_data: 原始ALS数据（输出）
 * @retval 状态：1/ERROR
 */
uint8_t LTR381_ReadALS_IR(LTR381_HandleTypeDef *hltr, uint32_t *als_data, uint32_t *ir_data)
{
	uint8_t raw_als[3]; // 存储0x0D~0x0F的3字节ALS数据

	// 1. 确保处于ALS模式（此时0x0D~0x0F为环境光数据）
	if (hltr->mode != LTR381_MODE_ALS)
	{
		if (LTR381_Config(hltr, LTR381_MODE_ALS, hltr->gain, hltr->resolution) != 1)
		{
			return 0;
		}
		delay_ms(10); // 模式切换延时
	}

	// 3. 读取0x0D~0x0F（ALS数据）
	if (LTR381_ReadMultiReg(LTR381_GREEN_DATA_0, raw_als, 3) != 1)
	{
		return 0;
	}

	*als_data = (raw_als[2] << 16) | (raw_als[1] << 8) | raw_als[0];

	// 3. 读取0x0D~0x0F（IR数据）
	if (LTR381_ReadMultiReg(LTR381_IR_DATA_0, raw_als, 3) != 1)
	{
		return 0;
	}

	*ir_data = (raw_als[2] << 16) | (raw_als[1] << 8) | raw_als[0];
	return 1;
}

/**
 * @brief  同步读取RGB+IR原始数据（地址0x0A~0x15，共12字节，确保同周期）
 * @param  hltr: 传感器句柄
 * @param  red/green/blue: 原始RGB数据（输出）
 * @param  ir: 原始IR数据（输出）
 * @retval 状态：1/ERROR
 */
uint8_t LTR381_ReadRGB_IR(LTR381_HandleTypeDef *hltr, uint32_t *red, uint32_t *green, uint32_t *blue, uint32_t *ir)
{
	uint8_t raw_data[12]; // 存储0x0A~0x15的12字节数据（IR+RGB）

	// 1. 确保处于RGB模式（此时0x0D~0x0F为绿色数据）
	if (hltr->mode != LTR381_MODE_RGB)
	{
		if (LTR381_Config(hltr, LTR381_MODE_RGB, hltr->gain, hltr->resolution) != 1)
		{
			return 0;
		}
		delay_ms(10); // 模式切换延时
	}

	// 2. 连续读取0x0A~0x15（12字节）：利用地址锁定机制确保数据同周期
	if (LTR381_ReadMultiReg(LTR381_IR_DATA_0, raw_data, 12) != 1)
	{
		return 0;
	}

	// 3. 解析数据（3字节组合：LSB->中字节->MSB，有效位16~20位）
	*ir = (raw_data[2] << 16) | (raw_data[1] << 8) | raw_data[0];	  // IR: 0x0A(0),0x0B(1),0x0C(2)
	*green = (raw_data[5] << 16) | (raw_data[4] << 8) | raw_data[3];  // 绿色: 0x0D(3),0x0E(4),0x0F(5)
	*red = (raw_data[8] << 16) | (raw_data[7] << 8) | raw_data[6];	  // 红色: 0x10(6),0x11(7),0x12(8)
	*blue = (raw_data[11] << 16) | (raw_data[10] << 8) | raw_data[9]; // 蓝色: 0x13(9),0x14(10),0x15(11)

	return 1;
}

/**
 * @brief  读取ALS环境光数据（复用0x0D~0x0F，此时为环境光数据）
 * @param  hltr: 传感器句柄
 * @param  als_data: 原始ALS数据（输出）
 * @retval 状态：1/ERROR
 */
uint8_t LTR381_ReadALS(LTR381_HandleTypeDef *hltr, uint32_t *als_data)
{
	uint8_t raw_als[3]; // 存储0x0D~0x0F的3字节ALS数据

	// 1. 确保处于ALS模式（此时0x0D~0x0F为环境光数据）
	if (hltr->mode != LTR381_MODE_ALS)
	{
		if (LTR381_Config(hltr, LTR381_MODE_ALS, hltr->gain, hltr->resolution) != 1)
		{
			return 0;
		}
		delay_ms(10); // 模式切换延时
	}

	// 3. 读取0x0D~0x0F（ALS数据）
	if (LTR381_ReadMultiReg(LTR381_GREEN_DATA_0, raw_als, 3) != 1)
	{
		return 0;
	}

	*als_data = (raw_als[2] << 16) | (raw_als[1] << 8) | raw_als[0];

	return 1;
}

/**
 * @brief  同步读取RGB原始数据（地址0x0D~0x15，共12字节，确保同周期）
 * @param  hltr: 传感器句柄
 * @param  red/green/blue: 原始RGB数据（输出）
 * @retval 状态：1/ERROR
 */
uint8_t LTR381_ReadRGB(LTR381_HandleTypeDef *hltr, uint32_t *red, uint32_t *green, uint32_t *blue)
{
	uint8_t raw_data[9]; // 存储0x0A~0x15的12字节数据（IR+RGB）

	// 1. 确保处于RGB模式（此时0x0D~0x0F为绿色数据）
	if (hltr->mode != LTR381_MODE_RGB)
	{
		if (LTR381_Config(hltr, LTR381_MODE_RGB, hltr->gain, hltr->resolution) != 1)
		{
			return 0;
		}
		delay_ms(10); // 模式切换延时
	}

	// 2. 连续读取0x0A~0x15（12字节）：利用地址锁定机制确保数据同周期
	if (LTR381_ReadMultiReg(LTR381_GREEN_DATA_0, raw_data, 12) != 1)
	{
		return 0;
	}

	// 3. 解析数据（3字节组合：LSB->中字节->MSB，有效位16~20位）
	*green = (raw_data[2] << 16) | (raw_data[1] << 8) | raw_data[0]; // 绿色: 0x0D(3),0x0E(4),0x0F(5)
	*red = (raw_data[5] << 16) | (raw_data[4] << 8) | raw_data[3];	 // 红色: 0x10(6),0x11(7),0x12(8)
	*blue = (raw_data[8] << 16) | (raw_data[7] << 8) | raw_data[6];	 // 蓝色: 0x13(9),0x14(10),0x15(11)

	return 1;
}

/**
 * @brief  配置LTR381传感器的中断功能，通过中断引脚控制LED点亮
 * @note   1. 中断触发后，引脚电平会保持有效状态（不读取MAIN_STATUS寄存器，中断标志不清除）
 *         2. 配置后，当ALS数据满足中断条件时，中断引脚拉低（低电平点亮LED）
 */
void LTR381_LED_ON(void)
{
	LTR381_WriteReg(0x19, 0x14);

	LTR381_WriteReg(0x21, 0);
	LTR381_WriteReg(0x22, 0);
	LTR381_WriteReg(0x23, 0);

	LTR381_WriteReg(0x24, 0xFF);
	LTR381_WriteReg(0x25, 0xFF);
	LTR381_WriteReg(0x26, 0xFF);
}
/**
 * @brief  关闭LTR381传感器的中断功能，停止通过中断引脚控制LED
 * @note   禁用中断后，中断引脚恢复默认电平（高电平LED熄灭）
 */
void LTR381_LED_OFF(void)
{
	LTR381_WriteReg(0x19, 0x00);
}

// ---------------------- 照度计算与颜色识别 ----------------------
// RGB校正函数
void LTR381_CalibrateRGB(uint32_t raw_r, uint32_t raw_g, uint32_t raw_b, uint32_t raw_ir,
						 uint32_t *calib_r, uint32_t *calib_g, uint32_t *calib_b)
{
	const float kR = 0.3f, kG = 0.1f, kB = 0.2f; // 红外干扰系数
	int32_t temp_r = (int32_t)raw_r - (int32_t)(raw_ir * kR);
	int32_t temp_g = (int32_t)raw_g - (int32_t)(raw_ir * kG);
	int32_t temp_b = (int32_t)raw_b - (int32_t)(raw_ir * kB);
	*calib_r = (temp_r > 0) ? temp_r : 0;
	*calib_g = (temp_g > 0) ? temp_g : 0;
	*calib_b = (temp_b > 0) ? temp_b : 0;
}

// ALS校正函数（基于原始ALS和IR数据）
float LTR381_GetLux_Calibrated(LTR381_HandleTypeDef *hltr, uint32_t raw_als, uint32_t raw_ir)
{
	float sensitivity = 3.0f; // 默认3X增益
	switch (hltr->gain)
	{
	case LTR381_GAIN_1X:
		sensitivity = 1.0f;
		break;
	case LTR381_GAIN_3X:
		sensitivity = 3.0f;
		break;
	case LTR381_GAIN_6X:
		sensitivity = 6.0f;
		break;
	case LTR381_GAIN_9X:
		sensitivity = 9.0f;
		break;
	case LTR381_GAIN_18X:
		sensitivity = 18.0f;
		break;
	}
	const float kIR_ALS = 0.8f; // ALS红外校正系数
	uint32_t calib_als = (uint32_t)((int32_t)raw_als - (int32_t)(raw_ir * kIR_ALS));
	calib_als = (calib_als > 0) ? calib_als : 0;
	return (float)calib_als * 0.6f / sensitivity;
}

void LTR381_test(void)
{
		uint32_t raw_r, raw_g, raw_b;

		// 读取RGB
		if (LTR381_ReadRGB(&ltr381, &raw_r, &raw_g, &raw_b) == 1)
		{
			printf("RGB: (R=%lu,G=%lu,B=%lu)\n", raw_r, raw_g, raw_b);
		}
		
		// 读取环境光
		// if (LTR381_ReadALS(&ltr381, &raw_als) == 1)
		// {
		// 	printf("ALS: =%lu \n", raw_als);
		// }

}

void LTR381_Init(void)
{
	printf("LTR381_Init id =%d\n", LTR381_ReadReg(LTR381_PART_ID));

	// 默认配置
	LTR381_Config(&ltr381, LTR381_MODE_RGB, LTR381_GAIN_3X, LTR381_RESOLUTION_100MS);

	LTR381_LED_ON(); // 打开LED

}
