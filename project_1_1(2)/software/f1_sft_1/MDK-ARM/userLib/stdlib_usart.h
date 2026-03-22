#ifndef __STDLIB_USART_H__
#define __STDLIB_USART_H__

#include "main.h"
#include "stdlib_common.h"

/* 串口协议相关缓冲区和超时配置。 */
#define DATA_MAX_LEN    32U
#define RX_BUF_SIZE     64U
#define DMA_BUF_SIZE    256U
#define TIMEOUT_MS      50U
#define PORT_MAX        3U

/* 各串口的收发模式配置，1 表示 DMA，0 表示中断。 */
#define USART1_RX_DMA   1
#define USART1_TX_DMA   1
#define USART2_RX_DMA   0
#define USART2_TX_DMA   1
#define USART3_RX_DMA   0
#define USART3_TX_DMA   0

/* 自定义协议回调，每收到一个字节就调用一次。 */
typedef void (*uartCustomParseCb_t)(uint8_t port, uint8_t byte);

/* 内置协议解析状态机。 */
typedef enum {
    PARSE_WAIT_SOF1 = 0,
    PARSE_WAIT_SOF2,
    PARSE_WAIT_CMD,
    PARSE_WAIT_LEN,
    PARSE_RECV_DATA,
    PARSE_WAIT_CRC8,
    PARSE_WAIT_TAIL,
} parseState_e;

/* 单个串口的协议配置。 */
typedef struct {
    uint8_t sof1;
    uint8_t sof2;
    uint8_t tail;
    uint8_t useCrc;
    uint8_t useTailCheck;
    uint8_t fixedLen;
    uint8_t fixedLenValue;
    uint8_t customProtocol;
} protocolCfg_t;

/* 单个串口实例的运行时上下文。 */
typedef struct {
    UART_HandleTypeDef *huart;
    protocolCfg_t       cfg;
    uartCustomParseCb_t parseCb;
    uint8_t             port;

    uint8_t  dmaBuf[DMA_BUF_SIZE];
    uint16_t dmaTail;

    uint8_t  rxBuf[RX_BUF_SIZE];
    uint16_t rxHead;
    uint16_t rxTail;
    uint8_t  rxByteTmp;

    uint8_t txBuf[DATA_MAX_LEN + 6U];

    parseState_e parseState;
    uint8_t      parseCmd;
    uint8_t      parseLen;
    uint8_t      parseData[DATA_MAX_LEN];
    uint8_t      parseDataIdx;
    uint8_t      parseCrc;

    uint32_t lastByteTickMs;

    uint8_t frameReady;
    uint8_t frameCmd;
    uint8_t frameLen;
    uint8_t frameData[DATA_MAX_LEN];

    uint32_t crcErrCnt;
    uint32_t syncErrCnt;
} uartRawInfo_t;

/* USART1，DMA 收发，使用标准协议。 */
#define UART_PORT1      0U

/* USART2，DMA 收发，使用固定长度帧。 */
#define UART_PORT2      1U

/* USART3，中断收发，使用自定义协议。 */
#define UART_PORT3      2U

/* 初始化所有串口上下文，并启动接收。 */
void STDLIB_USART_Init(void);

/* 在主循环中轮询串口接收并驱动协议解析。 */
void STDLIB_USART_Updata(void);

/* 为指定串口注册自定义协议字节回调。 */
void STDLIB_USART_SetCustomCb(uint8_t port, uartCustomParseCb_t cb);

/* 获取一帧已解析完成的数据，仅标准协议端口有效。 */
uint8_t STDLIB_USART_GetFrame(uint8_t port, uint8_t *cmd, uint8_t *data, uint8_t *len);

/* 按内置协议打包并发送一帧数据。 */
void STDLIB_USART_SendFrame(uint8_t port, uint8_t cmd, uint8_t *data, uint8_t len);

/* 直接发送原始字节流，供自定义协议使用。 */
void STDLIB_USART_SendRaw(uint8_t port, uint8_t *data, uint16_t len);

#endif
