#include "ps2/y_ps2.h"

u8 ps2_buf[9];

/* 软件I2C引脚定义 */
#define SCL_PORT GPIOB
#define SCL_PIN GPIO_Pin_6
#define SDA_PORT GPIOB
#define SDA_PIN GPIO_Pin_7

/* 超时参数定义 */
#define I2C_TIMEOUT_MAX 5000
static uint32_t I2C_TIMEOUT_UserCallback(uint8_t errorCode);

/* 引脚输出/输入模式切换 */
// SDA(PB11)输出模式：PB11设置为通用推挽输出
// #define PS2_SDA_OUT()                        \
//     {                                    \
//         GPIOA->CRL &= ~(0xF << (4 * 4)); \
//         GPIOA->CRL |= (0xB << (4 * 4));  \
//     }
// // SDA(PB11)输入模式：PB11设置为浮空输入
// #define PS2_SDA_IN()                         \
//     {                                    \
//         GPIOA->CRL &= ~(0xF << (4 * 4)); \
//         GPIOA->CRL |= (0x4 << (4 * 4));  \
//     }

#define PS2_SDA_OUT() \
    {             \
    }
// SDA(PB11)输入模式：PB11设置为浮空输入
#define PS2_SDA_IN() \
    {            \
    }

/* 电平操作宏 */
#define PS2_SCL_HIGH GPIO_SetBits(SCL_PORT, SCL_PIN)
#define PS2_SCL_LOW GPIO_ResetBits(SCL_PORT, SCL_PIN)
#define PS2_SDA_HIGH GPIO_SetBits(SDA_PORT, SDA_PIN)
#define PS2_SDA_LOW GPIO_ResetBits(SDA_PORT, SDA_PIN)
#define PS2_SDA_READ GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN)

// 延时函数（示例：6us，用于标准模式100kHz时序）
void PS2_I2C_Delay(void)
{
    volatile uint32_t i, j; // 加volatile
    for (i = 0; i < 6; i++) // 6us
    {
        for (j = 0; j < 5; j++)
            ;
    }
}

/* 软件I2C初始化 */
void PS2_I2C_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /* 使能GPIOA时钟（STM32F1系列I2C引脚在GPIOA，使用APB2总线时钟） */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* 配置SCL和SDA引脚为开漏输出（软件I2C必须使用开漏输出） */
    GPIO_InitStruct.GPIO_Pin = SCL_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;  // 开漏输出（F1系列模式定义）
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz; // F1系列常用50MHz高速模式
    GPIO_Init(SCL_PORT, &GPIO_InitStruct);

    // 配置SDA引脚，参数与SCL一致
    GPIO_InitStruct.GPIO_Pin = SDA_PIN;
    GPIO_Init(SDA_PORT, &GPIO_InitStruct);

    /* 初始电平：I2C总线空闲时，SCL和SDA均为高电平 */
    PS2_SCL_HIGH; // 需确保PS2_SCL_HIGH宏定义为GPIO_SetBits(SCL_PORT, SCL_PIN)
    PS2_SDA_HIGH; // 需确保SDA_HIGH宏定义为GPIO_SetBits(SDA_PORT, SDA_PIN)
}

/* 产生起始信号（SCL高电平时，SDA从高到低跳变） */
static void PS2_I2C_Start(void)
{
    PS2_SDA_OUT(); // SDA输出模式
    PS2_SDA_HIGH;
    PS2_SCL_HIGH;
    PS2_I2C_Delay();
    PS2_SDA_LOW; // 拉低SDA
    PS2_I2C_Delay();
    PS2_SCL_LOW; // 拉低SCL，准备传输数据
}

/* 产生停止信号（SCL高电平时，SDA从低到高跳变） */
static void PS2_I2C_Stop(void)
{
    PS2_SDA_OUT(); // SDA输出模式
    PS2_SCL_LOW;
    PS2_SDA_LOW;
    PS2_I2C_Delay();
    PS2_SCL_HIGH;
    PS2_I2C_Delay();
    PS2_SDA_HIGH; // 释放SDA
    PS2_I2C_Delay();
}

/* 等待从机应答（0=应答，1=非应答） */
static uint8_t PS2_I2C_WaitAck(void)
{
    uint32_t timeout = 0;
    PS2_SDA_IN(); // 切换SDA为输入
    PS2_SDA_HIGH; // 释放SDA
    PS2_I2C_Delay();
    PS2_SCL_HIGH; // 拉高SCL等待应答
    PS2_I2C_Delay();

    while (PS2_SDA_READ) // 等待SDA拉低（应答）
    {
        timeout++;
        if (timeout > I2C_TIMEOUT_MAX)
        {
            PS2_I2C_Stop();
            return 1; // 超时无应答
        }
    }
    PS2_SCL_LOW; // 拉低SCL结束应答检测
    PS2_I2C_Delay();
    return 0; // 收到应答
}

/* 主机产生应答信号（0=应答，1=非应答） */
static void I2C_Ack(uint8_t ack)
{
    PS2_SDA_OUT();
    if (ack)
        PS2_SDA_HIGH; // 非应答
    else
        PS2_SDA_LOW; // 应答
    PS2_I2C_Delay();
    PS2_SCL_HIGH;
    PS2_I2C_Delay();
    PS2_SCL_LOW;
    PS2_SDA_HIGH; // 释放SDA
    PS2_I2C_Delay();
}

/* 发送一个字节 */
static void PS2_I2C_SendByte(uint8_t data)
{
    PS2_SDA_OUT();
    PS2_SCL_LOW; // 拉低SCL准备发送
    PS2_I2C_Delay();
    for (uint8_t i = 0; i < 8; i++)
    {
        // 高位先发送
        if ((data & 0x80) >> 7)
            PS2_SDA_HIGH;
        else
            PS2_SDA_LOW;
        data <<= 1;
        PS2_SCL_HIGH; // 拉高SCL，从机读取数据
        PS2_I2C_Delay();
        PS2_SCL_LOW; // 拉低SCL，准备下一位
        PS2_I2C_Delay();
    }
}

/* 读取一个字节（ack=0：读完应答；ack=1：读完非应答） */
static uint8_t PS2_I2C_ReadByte(uint8_t ack)
{
    uint8_t data = 0;
    PS2_SDA_IN(); // 切换SDA为输入

    for (uint8_t i = 0; i < 8; i++)
    {
        PS2_SCL_HIGH; // 拉高SCL读取数据
        data <<= 1;
        if (PS2_SDA_READ)
            data |= 0x01;
        PS2_I2C_Delay();
        PS2_SCL_LOW;
        PS2_I2C_Delay();
    }

    I2C_Ack(ack); // 发送应答/非应答
    return data;
}

/* 软件I2C扫描函数 */
void i2c_scan_all(void)
{
    uint8_t device_exists;
    printf("   ");
    for (int col = 0; col < 0x10; ++col)
    {
        printf("0%1X ", col);
    }
    printf("\r\n");

    for (uint8_t row = 0; row < 0x08; ++row)
    {
        printf("%1X0 ", row);
        for (uint8_t col = 0; col < 0x10; ++col)
        {
            uint8_t addr = (row << 4) | col;
            device_exists = 0;

            PS2_I2C_Start();
            PS2_I2C_SendByte((addr << 1) | 0); // 发送写地址
            if (PS2_I2C_WaitAck() == 0)        // 收到应答则存在设备
            {
                device_exists = 1;
            }
            PS2_I2C_Stop();
            PS2_I2C_Delay(); // 延时避免总线冲突

            if (addr == 0x00)
            {
                printf("00* ");
            }
            else if (device_exists)
            {
                printf("%02X ", addr);
            }
            else
            {
                printf("__ ");
            }
        }
        printf("\r\n");
    }
}

/* 超时回调函数 */
static uint32_t I2C_TIMEOUT_UserCallback(uint8_t errorCode)
{
    //printf("iic errorCode = %d\r\n", errorCode);
    PS2_I2C_Stop(); // 超时后发送停止信号，释放总线
    return 0;
}

/* 初始化 */
uint8_t usb_ps2_Init(void)
{
    uint8_t ack = 0;

//    PS2_I2C_GPIO_Init();
    // 延时等待引脚电平稳定
    for (int i = 0; i < 100; i++)
    {
        PS2_I2C_Delay();
    }

    PS2_I2C_Start();
    PS2_I2C_SendByte(0x50 << 1 | 1); // 读地址
    ack = PS2_I2C_WaitAck();

    if (ack == 0)
        return 1; // 检测到设备
    else
        return 0; // 未检测到设备
}

// 检查PS2手柄数据是否全为0（未连接状态）
uint8_t ps2_is_connected(void)
{
		ps2_write_read();
    // 检查ps2_buf中是否有非0数据
    for (int i = 0; i < 9; i++) {
        if (ps2_buf[i] != 0) {
            return 1; // 有非0数据，手柄已连接
        }
    }
    return 0; // 全部为0，手柄未连接
}

void ps2_write_read(void)
{

    PS2_I2C_Start();
    PS2_I2C_SendByte(0x50 << 1 | 1); // 读地址
    if (PS2_I2C_WaitAck())           // 等待应答
    {
        I2C_TIMEOUT_UserCallback(21);
        return;
    }
    // 获取数据
    for (int i = 0; i < 8; i++)
    {
        ps2_buf[i] = PS2_I2C_ReadByte(0);
    }
    ps2_buf[8] = PS2_I2C_ReadByte(1);

    PS2_I2C_Stop(); // 产生一个停止条件

     // 打印数据
//     for (int i = 0; i < 9; i++)
//     {
//        printf("%u  ", ps2_buf[i]);
//     }
//     printf("\n");
}
