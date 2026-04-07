/*============================================================================
 * README
 *
 *============================================================================*/

#include "driver_tjcLCD.h"
#include <string.h>

/*============================================================================
 * 内部配置（仅driver_tjcLCD模块内部使用）
 *============================================================================*/

/* 帧头、帧尾定义 */
#define TJCLCD_SOF1             (0x55U)   /* 包头1 */
#define TJCLCD_SOF2             (0xAAU)   /* 包头2 */
#define TJCLCD_TAIL             (0xFFU)   /* 包尾 */

/* 文本控件发送缓冲区长度：控件名 + ".txt=\"" + 内容 + "\"" + 0xFF×3 */
#define TJCLCD_TEXT_TX_BUF_LEN  (TJCLCD_CTRL_NAME_MAX_LEN + 6U + TJCLCD_TEXT_CONTENT_MAX_LEN + 1U + 3U)

/* 接收状态机状态枚举 */
typedef enum {
  TJCLCD_PARSE_SOF1 = 0,   /* 等待包头1 */
  TJCLCD_PARSE_SOF2,        /* 等待包头2 */
  TJCLCD_PARSE_CTRL,        /* 接收控制字段 */
  TJCLCD_PARSE_DATA,        /* 接收数据内容 */
  TJCLCD_PARSE_CRC,         /* 接收CRC占位字节 */
  TJCLCD_PARSE_CNT,         /* 接收CNT占位字节 */
  TJCLCD_PARSE_TAIL,        /* 接收包尾 */
} tjcLcdParseState_e;

/* TJC串口屏模块数据 */
tjcLcdInfo_t tjcLcdInfo;

/* 接收状态机当前状态 */
static tjcLcdParseState_e tjcLcdParseState = TJCLCD_PARSE_SOF1;

/* 当前帧临时接收缓冲 */
static tjcLcdFrame_t tjcLcdRxTmp;

/* 数据字节接收计数 */
static uint8_t tjcLcdDataIdx = 0U;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* USART3自定义回调：逐字节解析固定16字节帧 */
static void __DRIVER_TJCLCD_UartByteCallback(uint8_t port, uint8_t byte){
  if(port != TJCLCD_DEP_UART_PORT) return;

  switch(tjcLcdParseState){
    case TJCLCD_PARSE_SOF1:
      if(byte == TJCLCD_SOF1){
        tjcLcdParseState = TJCLCD_PARSE_SOF2;
      }
      break;

    case TJCLCD_PARSE_SOF2:
      if(byte == TJCLCD_SOF2){
        tjcLcdParseState = TJCLCD_PARSE_CTRL;
      } else {
        tjcLcdParseState = TJCLCD_PARSE_SOF1;
      }
      break;

    case TJCLCD_PARSE_CTRL:
      tjcLcdRxTmp.ctrl = byte;
      tjcLcdDataIdx = 0U;
      tjcLcdParseState = TJCLCD_PARSE_DATA;
      break;

    case TJCLCD_PARSE_DATA:
      tjcLcdRxTmp.data[tjcLcdDataIdx++] = byte;
      if(tjcLcdDataIdx >= TJCLCD_DATA_LEN){
        tjcLcdParseState = TJCLCD_PARSE_CRC;
      }
      break;

    case TJCLCD_PARSE_CRC:
      /* CRC占位，不校验，直接跳过 */
      tjcLcdParseState = TJCLCD_PARSE_CNT;
      break;

    case TJCLCD_PARSE_CNT:
      /* CNT占位，直接跳过 */
      tjcLcdParseState = TJCLCD_PARSE_TAIL;
      break;

    case TJCLCD_PARSE_TAIL:
      if(byte == TJCLCD_TAIL){
        tjcLcdInfo.rxFrame = tjcLcdRxTmp;
        tjcLcdInfo.hasNewData = 1U;
      }
      tjcLcdParseState = TJCLCD_PARSE_SOF1;
      break;

    default:
      tjcLcdParseState = TJCLCD_PARSE_SOF1;
      break;
  }
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化TJC串口屏驱动，绑定USART3字节回调 */
void DRIVER_TJCLCD_Init(void){
  DRIVER_TJCLCD_Reset();
  TJCLCD_DEP_UART_SET_CUSTOM_CB(__DRIVER_TJCLCD_UartByteCallback);
}

/* 清空接收缓存与解析状态 */
void DRIVER_TJCLCD_Reset(void){
  memset(&tjcLcdInfo, 0, sizeof(tjcLcdInfo));
  memset(&tjcLcdRxTmp, 0, sizeof(tjcLcdRxTmp));
  tjcLcdParseState = TJCLCD_PARSE_SOF1;
  tjcLcdDataIdx = 0U;
}

/* 按帧格式打包并通过USART3发送一帧数据 */
void DRIVER_TJCLCD_Send(const tjcLcdFrame_t *frame){
  static uint8_t txBuf[TJCLCD_FRAME_LEN];

  if(frame == NULL) return;

  txBuf[0]  = TJCLCD_SOF1;                          /* 包头1 */
  txBuf[1]  = TJCLCD_SOF2;                          /* 包头2 */
  txBuf[2]  = frame->ctrl;                          /* 控制字段 */
  memcpy(&txBuf[3], frame->data, TJCLCD_DATA_LEN);  /* 数据内容 */
  txBuf[13] = 0x00U;                                /* CRC占位 */
  txBuf[14] = 0x00U;                                /* CNT占位 */
  txBuf[15] = TJCLCD_TAIL;                          /* 包尾 */

  TJCLCD_DEP_UART_SEND_RAW(txBuf, TJCLCD_FRAME_LEN);
}

/* 查询是否有新帧到达 */
uint8_t DRIVER_TJCLCD_HasNewData(void){
  return tjcLcdInfo.hasNewData;
}

/* 读取最近一次接收帧，并清除更新标志 */
uint8_t DRIVER_TJCLCD_GetRxFrame(tjcLcdFrame_t *frame){
  if(frame == NULL) return 0U;
  if(tjcLcdInfo.hasNewData == 0U) return 0U;

  *frame = tjcLcdInfo.rxFrame;
  tjcLcdInfo.hasNewData = 0U;
  return 1U;
}

/* 更新TJC串口屏文本控件，发送格式：控件名.txt="内容"+0xFF 0xFF 0xFF
 * ctrlName  : 控件名ASCII字符串，如 "t3"
 * content   : 文本内容字节流（GB2312编码），由调用方编码传入
 * contentLen: 内容字节数
 */
void DRIVER_TJCLCD_SendText(const char *ctrlName, const uint8_t *content, uint8_t contentLen){
  static uint8_t txBuf[TJCLCD_TEXT_TX_BUF_LEN];
  uint16_t idx = 0U;
  uint8_t nameLen;

  if(ctrlName == NULL) return;
  if(contentLen > TJCLCD_TEXT_CONTENT_MAX_LEN) return;

  nameLen = (uint8_t)strlen(ctrlName);
  if((nameLen == 0U) || (nameLen > TJCLCD_CTRL_NAME_MAX_LEN)) return;

  /* 控件名 */
  memcpy(&txBuf[idx], ctrlName, nameLen);
  idx += nameLen;
  /* .txt=" */
  txBuf[idx++] = (uint8_t)'.';
  txBuf[idx++] = (uint8_t)'t';
  txBuf[idx++] = (uint8_t)'x';
  txBuf[idx++] = (uint8_t)'t';
  txBuf[idx++] = (uint8_t)'=';
  txBuf[idx++] = (uint8_t)'"';
  /* 内容（GB2312编码字节流） */
  if((content != NULL) && (contentLen > 0U)){
    memcpy(&txBuf[idx], content, contentLen);
    idx += contentLen;
  }
  /* 闭合引号 */
  txBuf[idx++] = (uint8_t)'"';
  /* TJC协议结束符：三个0xFF */
  txBuf[idx++] = 0xFFU;
  txBuf[idx++] = 0xFFU;
  txBuf[idx++] = 0xFFU;

  TJCLCD_DEP_UART_SEND_RAW(txBuf, idx);
}
