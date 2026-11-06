#include "us01/us01.h"

// 颜色数组
static const Color_t color_table[] = {
    COLOR_RED,      // 索引 0
    COLOR_GREEN,    // 索引 1  
    COLOR_BLUE,     // 索引 2
    COLOR_YELLOW,   // 索引 3
    COLOR_PURPLE,   // 索引 4
    COLOR_CYAN,     // 索引 5
    COLOR_WHITE,    // 索引 6
    COLOR_OFF       // 索引 7
};

#define COLOR_COUNT (sizeof(color_table) / sizeof(color_table[0]))



/* 超时回调函数 */
static uint32_t I2C_TIMEOUT_UserCallback(uint8_t errorCode)
{
   // printf("us01 iic errorCode = %d\r\n", errorCode);
    i2c_stop();  // 超时后发送停止信号，释放总线
    return 0;
}

/* 幻彩超声波初始化 */
uint8_t US01_Init(void)
{
    uint8_t ack;
        
    // 验证0x2D地址是否为US01
    i2c_start();
    i2c_write_byte((0x2D << 1) | 0);  // 写操作
    ack = i2c_wait_ack();
    i2c_stop();
    
    if (ack == 0)
        return 1;  // 检测到设备
    else
        return 0;  // 未检测到设备
}



// R探头RGB灯控制
uint8_t us01_rgb_r(uint8_t r,uint8_t g,uint8_t b)
{
	i2c_start();
	i2c_write_byte(0x5a);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(0);
	i2c_write_byte(0x00);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(1);
	i2c_write_byte(r); // 写寄存器地址
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(2);
	i2c_stop();
	delay_us(1000);
	
	i2c_start();
	i2c_write_byte(0x5a);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(3);
	i2c_write_byte(0x01);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(4);
	i2c_write_byte(g); // 写寄存器地址
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(5);
	i2c_stop();
	delay_us(1000);
	
	i2c_start();
	i2c_write_byte(0x5a);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(6);
	i2c_write_byte(0x02);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(7);
	i2c_write_byte(b); // 写寄存器地址
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(8);
	i2c_stop();
	delay_us(1000);

    return 1;
}

// T探头RGB灯控制
uint8_t us01_rgb_t(uint8_t r,uint8_t g,uint8_t b)
{
	i2c_start();
	i2c_write_byte(0x5a);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(9);
	i2c_write_byte(0x03);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(10);
	i2c_write_byte(r); // 写寄存器地址
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(11);
	i2c_stop();
	delay_us(1000);
	
	i2c_start();
	i2c_write_byte(0x5a);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(12);
	i2c_write_byte(0x04);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(13);
	i2c_write_byte(g); // 写寄存器地址
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(14);
	i2c_stop();
	delay_us(1000);
	
	i2c_start();
	i2c_write_byte(0x5a);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(15);
	i2c_write_byte(0x05);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(16);
	i2c_write_byte(b); // 写寄存器地址
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(17);
	i2c_stop();
	delay_us(1000);

    return 1;
}

// 同时控制两个探头的RGB灯
uint8_t us01_rgb_both(Color_t color)
{
    uint8_t ret1 = us01_rgb_r(color.r, color.g, color.b);
    uint8_t ret2 = us01_rgb_t(color.r, color.g, color.b);
    return (ret1 && ret2);
}

/**
 * @brief 切换到指定索引的颜色
 * @param index 颜色索引 (0-7)
 */
void us01_set_color(uint8_t index)
{
    
   us01_rgb_both(color_table[index]);
    
    
}
/**
 *@beaf 超声波开始测距，需要等待100ms测量时间
 *@param 无
 */
uint8_t us01_start_measuring(void)
{
	//写地址为0X5a 寄存器地址0x10
	i2c_start();
	i2c_write_byte(0x5a);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(18);
	i2c_write_byte(0x10);
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(19);
	i2c_write_byte(1); //写命令0X01,0X01为开始测量命令 
	if (i2c_wait_ack()) return I2C_TIMEOUT_UserCallback(20);
	i2c_stop();

    return 1;
}


/**
 *@beaf 超声波测距 cm
 *@param 无
 *@retval distance 测试的距离
 */
float us01_read_distance(void)
{
	u8 value_H = 0, value_L = 0;
	uint16_t raw_data = 0;
	float distance = 0;

	// 读取距离数据
	i2c_start();
	i2c_write_byte(0X5b);
	if (i2c_wait_ack()) {
        i2c_stop();
        printf("读取距离失败: 地址无应答\n");
        return -1;
    }
	
	// 读取两个字节
	value_H = i2c_read_byte(1);	// 先读高位，发送ACK
	value_L = i2c_read_byte(0);	// 再读低位，发送NACK
	i2c_stop();
	
	// 合并数据
	raw_data = (value_H << 8) | value_L;
	
	
	// 计算距离：340m/s = 0.017cm/μs
	// 注意：声波往返时间，实际距离 = (时间 * 声速) / 2
	distance = raw_data * 0.017f;

	return distance;
}

