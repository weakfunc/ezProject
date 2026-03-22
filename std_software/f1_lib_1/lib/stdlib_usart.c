#include "stdlib_usart.h"


/* 串口端口号到 HAL 句柄的映射表。 */
static UART_HandleTypeDef *portMap[PORT_MAX] = {
    [UART_PORT1] = &huart1,
    [UART_PORT2] = &huart2,
    [UART_PORT3] = &huart3,
};

/* 串口运行时上下文。 */
uartRawInfo_t uartRawInfo[PORT_MAX];

/* 每个端口对应的协议预设。 */
static const protocolCfg_t cfgPreset[PORT_MAX] = {
    /* UART_PORT1: 标准协议，启用 CRC 和帧尾校验。 */
    {
        .sof1 = 0xAA,
        .sof2 = 0x55,
        .tail = 0x0D,
        .useCrc = 1,
        .useTailCheck = 1,
        .fixedLen = 0,
        .fixedLenValue = 0,
        .customProtocol = 0,
    },
    /* UART_PORT2: 自定义协议，由外部回调逐字节处理。 */
    {
        .customProtocol = 1,
    },
    /* UART_PORT3: 自定义协议，由外部回调逐字节处理。 */
    {
        .customProtocol = 1,
    },
};

/* 根据 HAL 串口句柄找到对应的运行时上下文。 */
static uartRawInfo_t *__STDLIB_USART_GetCtxByHuart(UART_HandleTypeDef *huart){
    for(uint8_t i = 0U; i < PORT_MAX; i++){
        if(uartRawInfo[i].huart == huart) return &uartRawInfo[i];
    }
    return NULL;
}

/* 判断指定串口实例的接收是否使用 DMA。 */
static uint8_t __STDLIB_USART_IsRxDma(USART_TypeDef *inst){
    if(inst == USART1) return USART1_RX_DMA;
    if(inst == USART2) return USART2_RX_DMA;
    if(inst == USART3) return USART3_RX_DMA;
    return 0U;
}

/* 判断指定串口实例的发送是否使用 DMA。 */
static uint8_t __STDLIB_USART_IsTxDma(USART_TypeDef *inst){
    if(inst == USART1) return USART1_TX_DMA;
    if(inst == USART2) return USART2_TX_DMA;
    if(inst == USART3) return USART3_TX_DMA;
    return 0U;
}

/* 将串口实例转换成人类可读的端口字符。 */
static uint8_t __STDLIB_USART_GetPortNum(UART_HandleTypeDef *huart){
    if(huart->Instance == USART1) return '1';
    if(huart->Instance == USART2) return '2';
    if(huart->Instance == USART3) return '3';
    return 'X';
}

/* 逐字节更新 CRC8 校验值。 */
static uint8_t __STDLIB_USART_Crc8Calc(uint8_t crc, uint8_t byte){
    crc ^= byte;
    for(uint8_t i = 0U; i < 8U; i++){
        crc = (crc & 0x80U) ? (uint8_t)((crc << 1U) ^ 0x07U) : (uint8_t)(crc << 1U);
    }
    return crc;
}

/* 等待串口发送状态回到 READY，避免新的发送请求覆盖旧事务。 */
static uint8_t __STDLIB_USART_WaitTxReady(UART_HandleTypeDef *huart){
    uint32_t startTickMs = HAL_GetTick();

    while(huart->gState != HAL_UART_STATE_READY){
        if((HAL_GetTick() - startTickMs) > TIMEOUT_MS){
            return 0U;
        }
    }
    return 1U;
}

/* 重置标准协议解析状态机。 */
static void __STDLIB_USART_ResetParse(uartRawInfo_t *c){
    c->parseState = PARSE_WAIT_SOF1;
    c->parseDataIdx = 0U;
    c->parseCrc = 0x00U;
}

/* 将当前解析缓存复制到帧缓冲，标记为可读取。 */
static void __STDLIB_USART_FrameComplete(uartRawInfo_t *c){
    c->frameCmd = c->parseCmd;
    c->frameLen = c->parseDataIdx;
    for(uint8_t i = 0U; i < c->parseDataIdx; i++){
        c->frameData[i] = c->parseData[i];
    }
    c->frameReady = 1U;
}

/* 向软件环形接收缓冲区压入 1 个字节。 */
static void __STDLIB_USART_RxPush(uartRawInfo_t *c, uint8_t byte){
    uint16_t next = (c->rxHead + 1U) & (RX_BUF_SIZE - 1U);
    if(next != c->rxTail){
        c->rxBuf[c->rxHead] = byte;
        c->rxHead = next;
    }
}

/* 从软件环形接收缓冲区取出 1 个字节。 */
static int8_t __STDLIB_USART_RxPop(uartRawInfo_t *c, uint8_t *byte){
    if(c->rxHead == c->rxTail) return -1;
    *byte = c->rxBuf[c->rxTail];
    c->rxTail = (c->rxTail + 1U) & (RX_BUF_SIZE - 1U);
    return 0;
}

/* 根据端口配置选择 DMA 或中断方式发送数据。 */
static void __STDLIB_USART_TxSend(uartRawInfo_t *c, uint8_t *data, uint16_t len){
    if(!__STDLIB_USART_WaitTxReady(c->huart)){
        return;
    }

    if(__STDLIB_USART_IsTxDma(c->huart->Instance)){
        HAL_UART_Transmit_DMA(c->huart, data, len);
    } else {
        HAL_UART_Transmit_IT(c->huart, data, len);
    }
}

/* 上电后发送固定初始化提示，便于观察串口是否工作正常。 */
static void __STDLIB_USART_SendInitInfo(uartRawInfo_t *c){
    uint8_t buf[25] = {
        'U', 'S', 'A', 'R', 'T', 'x', ' ',
        'I', 'N', 'I', 'T', ' ',
        'S', 'U', 'C', 'C', 'E', 'S', 'S', 'F', 'U', 'L', 'L', 'Y', '\n'
    };
    buf[5] = __STDLIB_USART_GetPortNum(c->huart);
    HAL_UART_Transmit(c->huart, buf, 25, 1000);
}

/* 标准协议的逐字节解析状态机。 */
static void __STDLIB_USART_ParseByte(uartRawInfo_t *c, uint8_t byte){
    c->lastByteTickMs = HAL_GetTick();

    switch(c->parseState){
    case PARSE_WAIT_SOF1:
        if(byte == c->cfg.sof1){
            c->parseCrc = __STDLIB_USART_Crc8Calc(0x00U, byte);
            c->parseState = PARSE_WAIT_SOF2;
        }
        break;

    case PARSE_WAIT_SOF2:
        if(byte == c->cfg.sof2){
            c->parseCrc = __STDLIB_USART_Crc8Calc(c->parseCrc, byte);
            c->parseState = PARSE_WAIT_CMD;
        } else {
            VAR_ADD(c->syncErrCnt)
            __STDLIB_USART_ResetParse(c);
            if(byte == c->cfg.sof1){
                c->parseCrc = __STDLIB_USART_Crc8Calc(0x00U, byte);
                c->parseState = PARSE_WAIT_SOF2;
            }
        }
        break;

    case PARSE_WAIT_CMD:
        if(c->cfg.fixedLen){
            c->parseCmd = 0U;
            c->parseLen = c->cfg.fixedLenValue;
            c->parseDataIdx = 0U;
            if(c->parseDataIdx < DATA_MAX_LEN){
                c->parseData[c->parseDataIdx++] = byte;
            }
            if(c->parseDataIdx >= c->parseLen){
                __STDLIB_USART_FrameComplete(c);
                __STDLIB_USART_ResetParse(c);
            } else {
                c->parseState = PARSE_RECV_DATA;
            }
        } else {
            c->parseCmd = byte;
            c->parseCrc = __STDLIB_USART_Crc8Calc(c->parseCrc, byte);
            c->parseState = PARSE_WAIT_LEN;
        }
        break;

    case PARSE_WAIT_LEN:
        c->parseCrc = __STDLIB_USART_Crc8Calc(c->parseCrc, byte);
        if(byte > DATA_MAX_LEN){
            VAR_ADD(c->syncErrCnt)
            __STDLIB_USART_ResetParse(c);
            break;
        }
        c->parseLen = byte;
        c->parseDataIdx = 0U;
        c->parseState = (c->parseLen == 0U) ? PARSE_WAIT_CRC8 : PARSE_RECV_DATA;
        break;

    case PARSE_RECV_DATA:
        c->parseData[c->parseDataIdx++] = byte;
        if(c->cfg.fixedLen){
            if(c->parseDataIdx >= c->parseLen){
                __STDLIB_USART_FrameComplete(c);
                __STDLIB_USART_ResetParse(c);
            }
        } else {
            c->parseCrc = __STDLIB_USART_Crc8Calc(c->parseCrc, byte);
            if(c->parseDataIdx >= c->parseLen){
                c->parseState = PARSE_WAIT_CRC8;
            }
        }
        break;

    case PARSE_WAIT_CRC8:
        if(c->cfg.useCrc){
            if(byte == c->parseCrc){
                c->parseState = PARSE_WAIT_TAIL;
            } else {
                VAR_ADD(c->crcErrCnt)
                __STDLIB_USART_ResetParse(c);
            }
        } else {
            if(c->parseDataIdx < DATA_MAX_LEN){
                c->parseData[c->parseDataIdx++] = byte;
            }
            c->parseState = PARSE_WAIT_TAIL;
        }
        break;

    case PARSE_WAIT_TAIL:
        if(c->cfg.useTailCheck){
            if(byte == c->cfg.tail){
                __STDLIB_USART_FrameComplete(c);
            } else {
                VAR_ADD(c->syncErrCnt)
            }
            __STDLIB_USART_ResetParse(c);
        } else {
            if(c->parseDataIdx < DATA_MAX_LEN){
                c->parseData[c->parseDataIdx++] = byte;
            }
            __STDLIB_USART_FrameComplete(c);
            __STDLIB_USART_ResetParse(c);
        }
        break;

    default:
        __STDLIB_USART_ResetParse(c);
        break;
    }
}

/* 为指定端口注册自定义协议回调。 */
void STDLIB_USART_SetCustomCb(uint8_t port, uartCustomParseCb_t cb){
    if(port < PORT_MAX){
        uartRawInfo[port].parseCb = cb;
    }
}

/* 初始化所有串口上下文，并启动接收。 */
void STDLIB_USART_Init(void){
    for(uint8_t i = 0U; i < PORT_MAX; i++){
        uartRawInfo_t *c = &uartRawInfo[i];
        c->huart = portMap[i];
        c->cfg = cfgPreset[i];
        c->port = i;
        c->rxHead = 0U;
        c->rxTail = 0U;
        c->dmaTail = 0U;
        c->frameReady = 0U;
        c->crcErrCnt = 0U;
        c->syncErrCnt = 0U;
        c->lastByteTickMs = 0U;
        __STDLIB_USART_ResetParse(c);

        if(__STDLIB_USART_IsRxDma(c->huart->Instance)){
            HAL_UART_Receive_DMA(c->huart, c->dmaBuf, DMA_BUF_SIZE);
        } else {
            HAL_UART_Receive_IT(c->huart, &c->rxByteTmp, 1U);
        }
    }

    for(uint8_t i = 0U; i < PORT_MAX; i++){
        __STDLIB_USART_SendInitInfo(&uartRawInfo[i]);
    }
}

/* 轮询 DMA 缓冲和软件缓冲，并驱动协议解析。 */
void STDLIB_USART_Updata(void){
    uint8_t byte;
    uint32_t now = HAL_GetTick();

    for(uint8_t i = 0U; i < PORT_MAX; i++){
        uartRawInfo_t *c = &uartRawInfo[i];

        if(__STDLIB_USART_IsRxDma(c->huart->Instance)){
            uint16_t dmaHead = (uint16_t)(DMA_BUF_SIZE -
                               (uint16_t)__HAL_DMA_GET_COUNTER(c->huart->hdmarx));
            while(c->dmaTail != dmaHead){
                __STDLIB_USART_RxPush(c, c->dmaBuf[c->dmaTail]);
                c->dmaTail = (c->dmaTail + 1U) % DMA_BUF_SIZE;
            }
        }

        if(!c->cfg.customProtocol){
            if(c->parseState != PARSE_WAIT_SOF1){
                if((now - c->lastByteTickMs) > TIMEOUT_MS){
                    VAR_ADD(c->syncErrCnt)
                    __STDLIB_USART_ResetParse(c);
                }
            }
        }

        while(__STDLIB_USART_RxPop(c, &byte) == 0){
            if(c->cfg.customProtocol){
                if(c->parseCb != NULL){
                    c->parseCb(c->port, byte);
                }
            } else {
                __STDLIB_USART_ParseByte(c, byte);
            }
        }
    }
}

/* 读取一帧已经完成解析的标准协议数据。 */
uint8_t STDLIB_USART_GetFrame(uint8_t port, uint8_t *cmd, uint8_t *data, uint8_t *len){
    if(port >= PORT_MAX) return 0U;
    uartRawInfo_t *c = &uartRawInfo[port];
    if(c->cfg.customProtocol || !c->frameReady) return 0U;
    c->frameReady = 0U;
    *cmd = c->frameCmd;
    *len = c->frameLen;
    for(uint8_t i = 0U; i < c->frameLen; i++){
        data[i] = c->frameData[i];
    }
    return 1U;
}

/* 按内置协议打包并发送一帧标准数据。 */
void STDLIB_USART_SendFrame(uint8_t port, uint8_t cmd, uint8_t *data, uint8_t len){
    if(port >= PORT_MAX) return;
    uartRawInfo_t *c = &uartRawInfo[port];
    if(c->cfg.customProtocol || len > DATA_MAX_LEN) return;

    uint8_t idx = 0U;
    c->txBuf[idx++] = c->cfg.sof1;
    c->txBuf[idx++] = c->cfg.sof2;

    if(c->cfg.fixedLen){
        for(uint8_t i = 0U; i < len; i++){
            c->txBuf[idx++] = data[i];
        }
    } else {
        uint8_t crc = 0x00U;
        crc = __STDLIB_USART_Crc8Calc(crc, c->cfg.sof1);
        crc = __STDLIB_USART_Crc8Calc(crc, c->cfg.sof2);
        c->txBuf[idx++] = cmd;
        crc = __STDLIB_USART_Crc8Calc(crc, cmd);
        c->txBuf[idx++] = len;
        crc = __STDLIB_USART_Crc8Calc(crc, len);
        for(uint8_t i = 0U; i < len; i++){
            c->txBuf[idx++] = data[i];
            crc = __STDLIB_USART_Crc8Calc(crc, data[i]);
        }
        c->txBuf[idx++] = crc;
        c->txBuf[idx++] = c->cfg.tail;
    }

    __STDLIB_USART_TxSend(c, c->txBuf, idx);
}

/* 发送原始字节流，不做任何协议封装。 */
void STDLIB_USART_SendRaw(uint8_t port, uint8_t *data, uint16_t len){
    if(port >= PORT_MAX || len == 0U) return;
    __STDLIB_USART_TxSend(&uartRawInfo[port], data, len);
}

/* 串口单字节中断接收完成后，将数据转入软件缓冲。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
    uartRawInfo_t *c = __STDLIB_USART_GetCtxByHuart(huart);
    if(c != NULL && !__STDLIB_USART_IsRxDma(huart->Instance)){
        __STDLIB_USART_RxPush(c, c->rxByteTmp);
        HAL_UART_Receive_IT(huart, &c->rxByteTmp, 1U);
    }
}

/* 串口出错后清状态并重新启动接收通道。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart){
    uartRawInfo_t *c = __STDLIB_USART_GetCtxByHuart(huart);
    if(c == NULL) return;

    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);

    if(__STDLIB_USART_IsRxDma(huart->Instance)){
        HAL_UART_Receive_DMA(huart, c->dmaBuf, DMA_BUF_SIZE);
        c->dmaTail = 0U;
    } else {
        HAL_UART_Receive_IT(huart, &c->rxByteTmp, 1U);
    }

    if(!c->cfg.customProtocol){
        __STDLIB_USART_ResetParse(c);
    }
}
