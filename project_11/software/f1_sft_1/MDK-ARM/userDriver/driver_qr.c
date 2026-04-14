/*============================================================================
 * README
 *
 *============================================================================*/

#include "driver_QR.h"

/*============================================================================
 * 内部配置（仅 driver_QR 模块内部使用）
 *============================================================================*/

/* 数据帧固定长度（字节） */
#define QR_FRAME_LEN       12U
/* 帧头第一字节 */
#define QR_SOF1            0x55U
/* 帧头第二字节 */
#define QR_SOF2            0x35U
/* 需提取数据在帧体（去帧头后）中的起始偏移，对应全帧第5字节 */
#define QR_BODY_DATA_OFF   3U

/* 解析状态机枚举 */
typedef enum {
  QR_STATE_WAIT_SOF1 = 0U, /* 等待帧头第一字节 */
  QR_STATE_WAIT_SOF2,      /* 等待帧头第二字节 */
  QR_STATE_RECV_BODY,      /* 接收帧体（去帧头后的10字节） */
} qrParseState_e;

/* 帧体长度（去掉2字节帧头） */
#define QR_BODY_LEN        (QR_FRAME_LEN - 2U)

/* 解析状态 */
static qrParseState_e qrParseState = QR_STATE_WAIT_SOF1;
/* 帧体接收计数 */
static uint8_t qrBodyIdx = 0U;
/* 帧体缓冲区 */
static uint8_t qrBodyBuf[QR_BODY_LEN];
/* 二维码模块数据 */
qrInfo_t qrInfo;

/* 将单个 ASCII 十六进制字符转换为 4bit 数值，失败返回 0xFF */
static uint8_t __DRIVER_QR_AsciiHexToNibble(uint8_t ch){
  if((ch >= (uint8_t)'0') && (ch <= (uint8_t)'9')) return ch - (uint8_t)'0';
  if((ch >= (uint8_t)'A') && (ch <= (uint8_t)'F')) return ch - (uint8_t)'A' + 10U;
  if((ch >= (uint8_t)'a') && (ch <= (uint8_t)'f')) return ch - (uint8_t)'a' + 10U;
  return 0xFFU;
}

/* 将4字节 ASCII 十六进制字符串解析为 uint32，字符非法时返回0 */
static uint32_t __DRIVER_QR_ParseHexStr(const uint8_t *buf){
  uint32_t result = 0U;
  uint8_t nibble;
  uint8_t i;

  for(i = 0U; i < 4U; i++){
    nibble = __DRIVER_QR_AsciiHexToNibble(buf[i]);
    if(nibble == 0xFFU) return 0U;
    result = (result << 4U) | (uint32_t)nibble;
  }
  return result;
}

/* USART2 自定义回调：按字节驱动定长帧解析状态机 */
static void __DRIVER_QR_UartByteCallback(uint8_t port, uint8_t byte){
  if(port != QR_DEP_UART_PORT) return;

  switch(qrParseState){
  case QR_STATE_WAIT_SOF1:
    if(byte == QR_SOF1){
      qrParseState = QR_STATE_WAIT_SOF2;
    }
    break;

  case QR_STATE_WAIT_SOF2:
    if(byte == QR_SOF2){
      qrBodyIdx = 0U;
      qrParseState = QR_STATE_RECV_BODY;
    } else {
      /* 非帧头字节，重新等待 SOF1 */
      qrParseState = QR_STATE_WAIT_SOF1;
      if(byte == QR_SOF1){
        qrParseState = QR_STATE_WAIT_SOF2;
      }
    }
    break;

  case QR_STATE_RECV_BODY:
    qrBodyBuf[qrBodyIdx++] = byte;
    if(qrBodyIdx >= QR_BODY_LEN){
      /* 帧接收完毕，将第5至第8字节的 ASCII 十六进制字符串解析为整数 */
      qrInfo.data = __DRIVER_QR_ParseHexStr(&qrBodyBuf[QR_BODY_DATA_OFF]);
      qrInfo.hasNewData = 1U;
      qrParseState = QR_STATE_WAIT_SOF1;
    }
    break;

  default:
    qrParseState = QR_STATE_WAIT_SOF1;
    break;
  }
}

/*============================================================================
 * API 接口
 *============================================================================*/

/* 初始化二维码扫描驱动，注册 USART2 字节回调 */
void DRIVER_QR_Init(void){
  DRIVER_QR_Reset();
  QR_DEP_UART_SET_CUSTOM_CB(__DRIVER_QR_UartByteCallback);
}

/* 清空缓存与解析状态 */
void DRIVER_QR_Reset(void){
  qrParseState     = QR_STATE_WAIT_SOF1;
  qrBodyIdx        = 0U;
  qrInfo.data      = 0U;
  qrInfo.hasNewData = 0U;
}

/* 返回是否有新数据到达 */
uint8_t DRIVER_QR_HasNewData(void){
  return qrInfo.hasNewData;
}

/* 获取最新二维码数据，有新数据返回1并清除标志，否则返回0 */
uint8_t DRIVER_QR_GetData(uint32_t *data){
  if(data == NULL) return 0U;
  if(qrInfo.hasNewData == 0U) return 0U;
  *data = qrInfo.data;
  qrInfo.hasNewData = 0U;
  return 1U;
}
