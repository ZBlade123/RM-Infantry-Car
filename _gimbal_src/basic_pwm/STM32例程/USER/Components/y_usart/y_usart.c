#include "y_usart/y_usart.h"

char uart_receive_buf[UART_NUM_MAX][UART_BUF_SIZE];   // 每串口独立缓冲
uint8_t uart_index[UART_NUM_MAX] = {0};         // 每串口独立索引
uint16_t uart_get_ok[UART_NUM_MAX] = {0};

/* 初始化串口1 */
void uart1_init(uint32_t BaudRate)
{
	USART_InitTypeDef USART_InitStructure;
	GPIO_InitTypeDef GPIO_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	/* 使能端口时钟 */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

	USART_DeInit(USART1);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;		/* PA.9 */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; /* 复用推挽输出 */
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	USART_InitStructure.USART_BaudRate = BaudRate;									/* 串口波特率 */
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;						/* 字长为8位数据格式 */
	USART_InitStructure.USART_StopBits = USART_StopBits_1;							/* 字长为8位数据格式 */
	USART_InitStructure.USART_Parity = USART_Parity_No;								/* 无奇偶校验位 */
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;					/* 收发模式 */
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; /* 无硬件数据流控制 */
	USART_Init(USART1, &USART_InitStructure);

	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; /* 抢占优先级 */
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;		  /* 子优先级 */
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			  /* IRQ通道使能 */
	NVIC_Init(&NVIC_InitStructure);

	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE); /* 开启串口接受中断 */
	USART_ITConfig(USART1, USART_IT_TXE, DISABLE); /* 禁止串口发送中断 */

	USART_Cmd(USART1, ENABLE); /* 使能串口1  */
}

void uart3_init(u32 baud)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	USART_InitStructure.USART_BaudRate = baud;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART3, &USART_InitStructure);

	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
	USART_ITConfig(USART3, USART_IT_TXE, DISABLE); /* 禁止串口发送中断 */

	USART_Cmd(USART3, ENABLE);
}



/***********************************************
	功能介绍：	串口1发送字节
	函数参数：	dat 发送的字节
	返回值：		无
 ***********************************************/
void uart1_send_byte(char dat)
{
	USART_SendData(USART1, dat);
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
}

/***********************************************
	功能介绍：	串口1发送字符串
	函数参数：	*s 发送的字符串
	返回值：		无
 ***********************************************/
void uart1_send_str(char *s)
{
	while (*s)
	{
		uart1_send_byte(*s++);
	}

}


/***********************************************
	功能介绍：	串口1发送数字
	函数参数：	tmp 发送的数字
	返回值：		无
 ***********************************************/
void uart1_send_int(int tmp)
{
	static char str[20];
	sprintf((char *)str, "%d", tmp);
	uart1_send_str(str);
}

/* 重定义fputc函数,写这个函数可以使用printf,记得开启Use MicroLIB */
int fputc(int ch, FILE *f)
{
	while ((USART1->SR & 0X40) == 0)
		; // 循环发送,直到发送完毕
	USART1->DR = (u8)ch;
	return ch;
}

void uart3_send_str(char *s)
{
	USART_ITConfig(UART5, USART_IT_RXNE, DISABLE);
	while (*s)
	{
		USART_SendData(USART3, *s++);
		while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET)
			;
	}
	USART_ITConfig(UART5, USART_IT_RXNE, ENABLE);
}



/***********************************************
	函数名称:	uart_data_parse()
	功能介绍:	解析串口接收的数据
	函数参数:	uart_data 串口接收的数据
				uart_num 串口号
	返回值:		无
 ***********************************************/
void uart_data_parse(char rx_data, uint8_t uart_num)
{
    if(uart_num >= UART_NUM_MAX) return;   // 越界保护
    if(rx_data == '#') {
        uart_index[uart_num] = 0;
        memset(uart_receive_buf[uart_num], 0, UART_BUF_SIZE);
        uart_receive_buf[uart_num][uart_index[uart_num]++] = rx_data;
    }
    else if(rx_data == '!') {
        if(uart_index[uart_num] < UART_BUF_SIZE - 1) {
            uart_receive_buf[uart_num][uart_index[uart_num]++] = rx_data;
            uart_receive_buf[uart_num][uart_index[uart_num]] = '\0';
        }
        uart_get_ok[uart_num] = 1;   // 直接按串口号索引
    }
    else {
        if(uart_index[uart_num] < UART_BUF_SIZE - 1) {
            uart_receive_buf[uart_num][uart_index[uart_num]++] = rx_data;
        }
    }
}

/* 串口1中断服务程序 */
void USART1_IRQHandler(void)
{
	// 先判断标志位
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
	{
		// 接收数据
		uint8_t sbuf_bak = USART_ReceiveData(USART1);

		uart_data_parse(sbuf_bak, 1); // 数据解析
		// 手动清除标志位
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}
}


void USART3_IRQHandler(void)
{
	//先判断标志位
	if (USART_GetITStatus(USART3, USART_IT_RXNE) == SET)
	{
		//接收数据
		uint8_t sbuf_bak = USART_ReceiveData(USART3);
		
		uart_data_parse(sbuf_bak,3);//数据解析
		//手动清除标志位
		USART_ClearITPendingBit(USART3, USART_IT_RXNE);  
	}

}



