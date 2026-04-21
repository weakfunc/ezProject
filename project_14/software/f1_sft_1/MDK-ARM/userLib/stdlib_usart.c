#include "stdlib_usart.h"
#include "usart.h"

/*============================================================================
 * 私有类型定义
 *============================================================================*/

/* 内置固定帧协议解析状态机状态。 */
typedef enum {
  PARSE_WAIT_SOF1 = 0, /* 等待包头 0x55 */
  PARSE_WAIT_SOF2,     /* 等待包头 0xAA */
  PARSE_WAIT_CMD,      /* 接收控制字段 */
  PARSE_RECV_DATA,     /* 接收 10 字节数据段 */
  PARSE_WAIT_CRC,      /* 接收 CRC8 校验字节 */
  PARSE_WAIT_CNT,      /* 接收帧计数字节（CNT） */
  PARSE_WAIT_TAIL,     /* 接收包尾 0xFF */
} parseState_e;

/* 串口端口协议配置（私有）。 */
typedef struct {
  uint8_t customProtocol; /* 1：使用外部字节回调；0：使用内置固定帧协议 */
} protocolCfg_t;

/* 单个串口实例的运行时上下文（私有）。 */
typedef struct {
  UART_HandleTypeDef *huart;            /* HAL 句柄 */
  protocolCfg_t       cfg;              /* 协议配置 */
  uartCustomParseCb_t parseCb;          /* 自定义字节回调 */
  uint8_t             port;             /* 端口索引 */

  uint8_t  dmaBuf[DMA_BUF_SIZE];        /* DMA 接收缓冲 */
  uint16_t dmaTail;                     /* DMA 缓冲读指针 */

  uint8_t  rxBuf[RX_BUF_SIZE];          /* 软件环形接收缓冲 */
  uint16_t rxHead;                      /* 环形缓冲写指针 */
  uint16_t rxTail;                      /* 环形缓冲读指针 */
  uint8_t  rxByteTmp;                   /* 中断模式单字节临时缓冲 */

  uint8_t txBuf[FRAME_LEN];             /* 发送帧缓冲（static-alike，防 DMA 传输期间栈帧失效） */

  parseState_e parseState;              /* 解析状态机当前状态 */
  uint8_t      parseCmd;                /* 当前帧控制字段 */
  uint8_t      parseData[STM32_DATA_LEN]; /* 当前帧数据段临时缓冲 */
  uint8_t      parseDataIdx;            /* 数据段已接收字节数 */
  uint8_t      parseCrc;                /* CRC8 累计值（覆盖 byte[0]~byte[12]） */

  uint32_t lastByteTickMs;              /* 最近一字节到达时刻（毫秒） */

  uint32_t crcErrCnt;                   /* CRC 校验错误计数 */
  uint32_t syncErrCnt;                  /* 帧同步错误计数 */
} uartRawInfo_t;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 串口端口号到 HAL 句柄的映射表。 */
static UART_HandleTypeDef *portMap[PORT_MAX] = {
  [UART_PORT1] = &huart1,
  [UART_PORT2] = &huart2,
  [UART_PORT3] = &huart3,
};

/* 串口运行时上下文（私有）。 */
uartRawInfo_t uartRawInfo[PORT_MAX];

/* 各串口对外数据接口（公有，供上层直接访问）。 */
usartInfo_t usartInfo[PORT_MAX];

/* 每个端口的协议预设。 */
static const protocolCfg_t cfgPreset[PORT_MAX] = {
  /* UART_PORT1：使用内置标准协议，由 stdlib_usart 解析，driver_ble 直接读 usartInfo。 */
  { .customProtocol = 0 },
  /* UART_PORT2：自定义协议，由 driver_qr 逐字节处理。 */
  { .customProtocol = 1 },
  /* UART_PORT3：自定义协议，由 driver_tjcLCD 逐字节处理。 */
  { .customProtocol = 1 },
};

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 根据 HAL 串口句柄查找对应的运行时上下文。 */
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

/* 将串口实例转换为人类可读的端口字符，用于初始化提示信息。 */
static uint8_t __STDLIB_USART_GetPortNum(UART_HandleTypeDef *huart){
  if(huart->Instance == USART1) return '1';
  if(huart->Instance == USART2) return '2';
  if(huart->Instance == USART3) return '3';
  return 'X';
}

/* 逐字节更新 CRC8 校验值（多项式 0x07）。 */
static uint8_t __STDLIB_USART_Crc8Calc(uint8_t crc, uint8_t byte){
  crc ^= byte;
  for(uint8_t i = 0U; i < 8U; i++){
    crc = (crc & 0x80U) ? (uint8_t)((crc << 1U) ^ 0x07U) : (uint8_t)(crc << 1U);
  }
  return crc;
}

/* 等待串口发送状态回到 READY，超时返回 0。 */
static uint8_t __STDLIB_USART_WaitTxReady(UART_HandleTypeDef *huart){
  uint32_t startTickMs = HAL_GetTick();
  while(huart->gState != HAL_UART_STATE_READY){
    if((HAL_GetTick() - startTickMs) > TIMEOUT_MS){
      return 0U;
    }
  }
  return 1U;
}

/* 重置内置协议解析状态机。 */
static void __STDLIB_USART_ResetParse(uartRawInfo_t *c){
  c->parseState   = PARSE_WAIT_SOF1;
  c->parseDataIdx = 0U;
  c->parseCrc     = 0x00U;
}

/* 将当前解析缓存写入对外数据接口，标记新帧就绪。 */
static void __STDLIB_USART_FrameComplete(uartRawInfo_t *c){
  usartInfo_t *info = &usartInfo[c->port];
  info->cmd = c->parseCmd;
  for(uint8_t i = 0U; i < STM32_DATA_LEN; i++){
    info->standardRxFrame.raw[i] = c->parseData[i];
  }
  info->frame_ready = true;
}

/* 向软件环形接收缓冲区压入 1 字节。 */
static void __STDLIB_USART_RxPush(uartRawInfo_t *c, uint8_t byte){
  uint16_t next = (c->rxHead + 1U) & (RX_BUF_SIZE - 1U);
  if(next != c->rxTail){
    c->rxBuf[c->rxHead] = byte;
    c->rxHead = next;
  }
}

/* 从软件环形接收缓冲区取出 1 字节，无数据返回 -1。 */
static int8_t __STDLIB_USART_RxPop(uartRawInfo_t *c, uint8_t *byte){
  if(c->rxHead == c->rxTail) return -1;
  *byte = c->rxBuf[c->rxTail];
  c->rxTail = (c->rxTail + 1U) & (RX_BUF_SIZE - 1U);
  return 0;
}

/* 按端口配置选择 DMA 或中断方式发送数据。 */
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

/* 上电后发送固定初始化提示，便于确认串口工作正常。 */
static void __STDLIB_USART_SendInitInfo(uartRawInfo_t *c){
  uint8_t buf[25] = {
    'U', 'S', 'A', 'R', 'T', 'x', ' ',
    'I', 'N', 'I', 'T', ' ',
    'S', 'U', 'C', 'C', 'E', 'S', 'S', 'F', 'U', 'L', 'L', 'Y', '\n'
  };
  buf[5] = __STDLIB_USART_GetPortNum(c->huart);
  HAL_UART_Transmit(c->huart, buf, 25, 1000);
}

/* 内置固定 16 字节帧协议的逐字节解析状态机。
 * 帧结构：[0x55][0xAA][CMD][D0..D9][CRC][CNT][0xFF]
 * CRC8 覆盖范围：byte[0]~byte[12]（共 13 字节）。
 */
static void __STDLIB_USART_ParseByte(uartRawInfo_t *c, uint8_t byte){
  c->lastByteTickMs = HAL_GetTick();

  switch(c->parseState){
  case PARSE_WAIT_SOF1:
    if(byte == 0x55U){
      c->parseCrc = __STDLIB_USART_Crc8Calc(0x00U, byte);
      c->parseState = PARSE_WAIT_SOF2;
    }
    break;

  case PARSE_WAIT_SOF2:
    if(byte == 0xAAU){
      c->parseCrc = __STDLIB_USART_Crc8Calc(c->parseCrc, byte);
      c->parseState = PARSE_WAIT_CMD;
    } else {
      VAR_ADD(c->syncErrCnt)
      __STDLIB_USART_ResetParse(c);
      /* 容错：若当前字节恰好为下一帧包头1，直接进入下一帧解析。 */
      if(byte == 0x55U){
        c->parseCrc = __STDLIB_USART_Crc8Calc(0x00U, byte);
        c->parseState = PARSE_WAIT_SOF2;
      }
    }
    break;

  case PARSE_WAIT_CMD:
    c->parseCmd = byte;
    c->parseCrc = __STDLIB_USART_Crc8Calc(c->parseCrc, byte);
    c->parseDataIdx = 0U;
    c->parseState = PARSE_RECV_DATA;
    break;

  case PARSE_RECV_DATA:
    c->parseData[c->parseDataIdx++] = byte;
    c->parseCrc = __STDLIB_USART_Crc8Calc(c->parseCrc, byte);
    if(c->parseDataIdx >= STM32_DATA_LEN){
      c->parseState = PARSE_WAIT_CRC;
    }
    break;

  case PARSE_WAIT_CRC:
#if ENABLE_CRC
    if(byte == c->parseCrc){
      c->parseState = PARSE_WAIT_CNT;
    } else {
      VAR_ADD(c->crcErrCnt)
      __STDLIB_USART_ResetParse(c);
    }
#else
    /* CRC 校验禁用，跳过本字节。 */
    c->parseState = PARSE_WAIT_CNT;
#endif
    break;

  case PARSE_WAIT_CNT:
    /* CNT 字节，仅接收不处理，直接跳过。 */
    c->parseState = PARSE_WAIT_TAIL;
    break;

  case PARSE_WAIT_TAIL:
    if(byte == 0xFFU){
      __STDLIB_USART_FrameComplete(c);
    } else {
      VAR_ADD(c->syncErrCnt)
    }
    __STDLIB_USART_ResetParse(c);
    break;

  default:
    __STDLIB_USART_ResetParse(c);
    break;
  }
}

/*============================================================================
 * API接口实现
 *============================================================================*/

/* 为指定端口注册自定义协议字节回调。 */
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
    c->cfg   = cfgPreset[i];
    c->port  = i;
    c->rxHead = 0U;
    c->rxTail = 0U;
    c->dmaTail = 0U;
    c->crcErrCnt  = 0U;
    c->syncErrCnt = 0U;
    c->lastByteTickMs = 0U;
    __STDLIB_USART_ResetParse(c);

    usartInfo[i].tx_cnt      = 0U;
    usartInfo[i].cmd         = 0U;
    usartInfo[i].frame_ready = false;

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

/* 轮询 DMA 缓冲和软件缓冲，驱动协议解析。 */
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

    /* 非自定义协议端口：检测超时，超时则重置解析状态机。 */
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

/* 按标准协议封装 usartInfo[port].standardTxFrame 并发送一帧。
 * 帧结构：[0x55][0xAA][cmd][standardTxFrame(10字节)][CRC/0x00][tx_cnt][0xFF]
 * 仅对非自定义协议端口有效。
 */
void STDLIB_USART_SendFrame(uint8_t port, uint8_t cmd){
  if(port >= PORT_MAX) return;
  uartRawInfo_t *c    = &uartRawInfo[port];
  usartInfo_t   *info = &usartInfo[port];
  if(c->cfg.customProtocol) return;

  c->txBuf[0] = 0x55U;
  c->txBuf[1] = 0xAAU;
  c->txBuf[2] = cmd;
  for(uint8_t i = 0U; i < STM32_DATA_LEN; i++){
    c->txBuf[3U + i] = info->standardTxFrame.raw[i];
  }

#if ENABLE_CRC
  uint8_t crc = 0x00U;
  for(uint8_t i = 0U; i < 13U; i++){
    crc = __STDLIB_USART_Crc8Calc(crc, c->txBuf[i]);
  }
  c->txBuf[13] = crc;
#else
  c->txBuf[13] = 0x00U;
#endif

  c->txBuf[14] = info->tx_cnt++;
  c->txBuf[15] = 0xFFU;

  __STDLIB_USART_TxSend(c, c->txBuf, FRAME_LEN);
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
