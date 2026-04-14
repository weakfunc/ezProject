#ifndef __STDLIB_USART_H__
#define __STDLIB_USART_H__

#include "main.h"
#include "stdlib_common.h"
#include <stdbool.h>

/*============================================================================
 * 内部配置
 *============================================================================*/

/* 接收环形缓冲区大小，必须为 2 的幂次。 */
#define RX_BUF_SIZE     64U
/* DMA 接收缓冲区大小。 */
#define DMA_BUF_SIZE    128U
/* 接收超时（毫秒），超时后重置解析状态机。 */
#define TIMEOUT_MS      50U
/* 串口端口总数。 */
#define PORT_MAX        3U

/* 各串口收发模式配置：1 = DMA，0 = 中断。 */
#define USART1_RX_DMA   0
#define USART1_TX_DMA   0
#define USART2_RX_DMA   0
#define USART2_TX_DMA   0
#define USART3_RX_DMA   0
#define USART3_TX_DMA   0

/*============================================================================
 * API接口
 *============================================================================*/

/* 固定帧总长度（字节）：帧头2 + CMD1 + 数据10 + CRC1 + CNT1 + 帧尾1 = 16。 */
#define FRAME_LEN       16U

/* 数据段长度（byte[3]~byte[12]，共 10 字节）。 */
#define STM32_DATA_LEN  10U

/* CRC8 校验使能：1 启用，0 禁用；禁用时 CRC 字节固定填 0x00。 */
#define ENABLE_CRC      0U

/* USART1，DMA 收发（由 driver_ble 通过自定义回调处理）。 */
#define UART_PORT1      0U
/* USART2，DMA 收发（由 driver_qr 通过自定义回调处理）。 */
#define UART_PORT2      1U
/* USART3，中断收发（由 driver_imu901 通过自定义回调处理）。 */
#define UART_PORT3      2U

/* 自定义协议字节回调类型，每收到一字节触发一次。 */
typedef void (*uartCustomParseCb_t)(uint8_t port, uint8_t byte);

/* 启用匿名结构体支持（ARMCC 默认关闭）。 */
#pragma anon_unions

/* 10 字节数据段联合体，支持按原始字节或具名字段访问。 */
typedef union {
  uint8_t raw[STM32_DATA_LEN];
  struct __attribute__((packed)) {
    uint32_t var_4b_1; /* 4 字节变量 1（字节 0~3） */
    uint32_t var_4b_2; /* 4 字节变量 2（字节 4~7） */
    uint8_t  var_1b_1; /* 1 字节变量 1（字节 8） */
    uint8_t  var_1b_2; /* 1 字节变量 2（字节 9） */
  };
} standardFrame_t;

/* 单个串口的对外数据接口。 */
typedef struct {
  /* --- 发送状态 --- */
  uint8_t         tx_cnt;          /* 发送帧计数器，0x00~0xFF 循环递增 */

  /* --- 接收状态 --- */
  uint8_t         cmd;             /* 最近一帧的控制字段（byte[2]） */
  standardFrame_t standardRxFrame; /* 接收数据段，支持具名字段访问；读取后应清零 frame_ready */
  standardFrame_t standardTxFrame; /* 发送数据段，上层填充后调用 STDLIB_USART_SendFrame 发出 */
  bool            frame_ready;     /* 新帧就绪标志；上层读取后应清零 */
} usartInfo_t;

/* 各串口对外数据接口，上层可直接访问。 */
extern usartInfo_t usartInfo[PORT_MAX];

/* 初始化所有串口上下文并启动接收。 */
void STDLIB_USART_Init(void);

/* 在主循环中轮询 DMA/中断缓冲，驱动协议解析状态机。 */
void STDLIB_USART_Updata(void);

/* 为指定串口注册自定义协议字节回调。 */
void STDLIB_USART_SetCustomCb(uint8_t port, uartCustomParseCb_t cb);

/* 按标准协议封装 usartInfo[port].standardTxFrame 并发送一帧，仅对非自定义协议端口有效。 */
void STDLIB_USART_SendFrame(uint8_t port, uint8_t cmd);

/* 直接发送原始字节流，不做任何协议封装，供自定义协议使用。 */
void STDLIB_USART_SendRaw(uint8_t port, uint8_t *data, uint16_t len);

#endif
