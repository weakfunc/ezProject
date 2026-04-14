/*============================================================================
 * README
 *
 *============================================================================*/

/*============================================================================
 * MAIXCAM串口协议（固定16字节）
 * [0]     0x55        包头1
 * [1]     0xAA        包头2
 * [2]     0x01        控制字段
 * [3~12]  data        二维码内容（10字节，不足补0x00）
 * [13]    CRC8        0x00时直接通过，否则校验[0]~[12]（多项式0x07）
 * [14]    CNT         包计数（0x00~0xFF循环递增）
 * [15]    0xFF        包尾
 *============================================================================*/

#include "driver_verison.h"
#include <string.h>

/* MAIXCAM模块数据 */
verisonInfo_t verisonInfo;

/*============================================================================
 * 内部配置（仅driver_verison模块内部使用）
 *============================================================================*/

/* 协议包头第1字节 */
#define VERISON_FRAME_SOF1                          (0x55U)
/* 协议包头第2字节 */
#define VERISON_FRAME_SOF2                          (0xAAU)
/* 协议控制字段固定值 */
#define VERISON_FRAME_CTRL                          (0x01U)
/* CRC8初始值 */
#define VERISON_CRC8_INIT                           (0x00U)
/* 协议包尾 */
#define VERISON_FRAME_TAIL                          (0xFFU)

/* MAIXCAM解析状态机 */
typedef enum {
  VERISON_STATE_WAIT_SOF1 = 0U,
  VERISON_STATE_WAIT_SOF2,
  VERISON_STATE_WAIT_CTRL,
  VERISON_STATE_RECV_DATA,
  VERISON_STATE_WAIT_CRC8,
  VERISON_STATE_WAIT_CNT,
  VERISON_STATE_WAIT_TAIL,
} verisonParseState_e;

/* 当前解析状态 */
static verisonParseState_e verisonParseState = VERISON_STATE_WAIT_SOF1;
/* 数据段接收计数 */
static uint8_t verisonDataIdx = 0U;
/* 最近一次解析成功的MAIXCAM数据缓存 */
static verisonInfo_t verisonInfoCache;
/* 当前帧临时控制字段 */
static uint8_t verisonCtrlTmp = 0U;
/* 当前帧临时数据段 */
static uint8_t verisonDataTmp[VERISON_DATA_LEN];
/* 当前帧临时CRC8 */
static uint8_t verisonCrc8Tmp = 0U;
/* 当前帧临时包计数 */
static uint8_t verisonCntTmp = 0U;
/* 当前帧CRC8累计计算值 */
static uint8_t verisonCrc8Calc = VERISON_CRC8_INIT;
/* 当前帧CRC8是否通过 */
static uint8_t verisonCrc8PassFlag = 0U;
/* CRC计算错误包总计数 */
static uint32_t verisonCrcErrTotalCnt = 0U;
/* 接收完整包总计数 */
static uint32_t verisonRxTotalCnt = 0U;

/* 按多项式0x07逐字节更新CRC8 */
static uint8_t __DRIVER_VERISON_Crc8Calc(uint8_t crc, uint8_t byte){
  uint8_t i;

  crc ^= byte;
  for(i = 0U; i < 8U; i++){
    if((crc & 0x80U) != 0U){
      crc = (uint8_t)((crc << 1U) ^ 0x07U);
    } else {
      crc = (uint8_t)(crc << 1U);
    }
  }
  return crc;
}

/* 同步累计统计值到缓存和对外结构体 */
static void __DRIVER_VERISON_SyncTotalCnt(void){
  verisonInfoCache.crcErrTotalCnt = verisonCrcErrTotalCnt;
  verisonInfoCache.rxTotalCnt = verisonRxTotalCnt;
  verisonInfo.crcErrTotalCnt = verisonCrcErrTotalCnt;
  verisonInfo.rxTotalCnt = verisonRxTotalCnt;
}

/* 保存最近一次解析成功的MAIXCAM数据 */
static void __DRIVER_VERISON_SaveFrame(void){
  uint8_t i;

  verisonInfoCache.ctrl = verisonCtrlTmp;
  verisonInfoCache.crc8 = verisonCrc8Tmp;
  verisonInfoCache.cnt = verisonCntTmp;
  for(i = 0U; i < VERISON_DATA_LEN; i++){
    verisonInfoCache.data[i] = verisonDataTmp[i];
  }
  __DRIVER_VERISON_SyncTotalCnt();
  verisonInfo = verisonInfoCache;
}

/* 重置MAIXCAM解析状态机 */
static void __DRIVER_VERISON_ResetParseState(void){
  verisonParseState = VERISON_STATE_WAIT_SOF1;
  verisonDataIdx = 0U;
  verisonCtrlTmp = 0U;
  verisonCrc8Tmp = 0U;
  verisonCntTmp = 0U;
  verisonCrc8Calc = VERISON_CRC8_INIT;
  verisonCrc8PassFlag = 0U;
}

/* USART2自定义字节回调，按固定16字节协议解析MAIXCAM数据 */
static void __DRIVER_VERISON_UartByteCallback(uint8_t port, uint8_t byte){
  if(port != VERISON_DEP_UART_PORT) return;

  switch(verisonParseState){
  case VERISON_STATE_WAIT_SOF1:
    if(byte == VERISON_FRAME_SOF1){
      verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(VERISON_CRC8_INIT, byte);
      verisonParseState = VERISON_STATE_WAIT_SOF2;
    }
    break;

  case VERISON_STATE_WAIT_SOF2:
    if(byte == VERISON_FRAME_SOF2){
      verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(verisonCrc8Calc, byte);
      verisonParseState = VERISON_STATE_WAIT_CTRL;
    } else {
      __DRIVER_VERISON_ResetParseState();
      if(byte == VERISON_FRAME_SOF1){
        verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(VERISON_CRC8_INIT, byte);
        verisonParseState = VERISON_STATE_WAIT_SOF2;
      }
    }
    break;

  case VERISON_STATE_WAIT_CTRL:
    if(byte == VERISON_FRAME_CTRL){
      verisonCtrlTmp = byte;
      memset(verisonDataTmp, 0, sizeof(verisonDataTmp));
      verisonDataIdx = 0U;
      verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(verisonCrc8Calc, byte);
      verisonParseState = VERISON_STATE_RECV_DATA;
    } else {
      __DRIVER_VERISON_ResetParseState();
      if(byte == VERISON_FRAME_SOF1){
        verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(VERISON_CRC8_INIT, byte);
        verisonParseState = VERISON_STATE_WAIT_SOF2;
      }
    }
    break;

  case VERISON_STATE_RECV_DATA:
    verisonDataTmp[verisonDataIdx++] = byte;
    verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(verisonCrc8Calc, byte);
    if(verisonDataIdx >= VERISON_DATA_LEN){
      verisonParseState = VERISON_STATE_WAIT_CRC8;
    }
    break;

  case VERISON_STATE_WAIT_CRC8:
    verisonCrc8Tmp = byte;
    if(byte == 0x00U){
      verisonCrc8PassFlag = 1U;
    } else {
      verisonCrc8PassFlag = (byte == verisonCrc8Calc) ? 1U : 0U;
    }
    verisonParseState = VERISON_STATE_WAIT_CNT;
    break;

  case VERISON_STATE_WAIT_CNT:
    verisonCntTmp = byte;
    verisonParseState = VERISON_STATE_WAIT_TAIL;
    break;

  case VERISON_STATE_WAIT_TAIL:
    if(byte == VERISON_FRAME_TAIL){
      verisonRxTotalCnt++;
      if(verisonCrc8PassFlag != 0U){
        __DRIVER_VERISON_SaveFrame();
        verisonInfo.hasNewData = 1U;
      } else {
        verisonCrcErrTotalCnt++;
        __DRIVER_VERISON_SyncTotalCnt();
      }
    }
    __DRIVER_VERISON_ResetParseState();
    if(byte == VERISON_FRAME_SOF1){
      verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(VERISON_CRC8_INIT, byte);
      verisonParseState = VERISON_STATE_WAIT_SOF2;
    }
    break;

  default:
    __DRIVER_VERISON_ResetParseState();
    break;
  }
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化MAIXCAM驱动并注册USART2解析回调 */
void DRIVER_VERISON_Init(void){
  DRIVER_VERISON_Reset();
  VERISON_DEP_UART_SET_CUSTOM_CB(__DRIVER_VERISON_UartByteCallback);
}

/* 清空MAIXCAM缓存和解析状态 */
void DRIVER_VERISON_Reset(void){
  memset(&verisonInfoCache, 0, sizeof(verisonInfoCache));
  memset(&verisonInfo, 0, sizeof(verisonInfo));
  memset(verisonDataTmp, 0, sizeof(verisonDataTmp));
  verisonCrcErrTotalCnt = 0U;
  verisonRxTotalCnt = 0U;
  __DRIVER_VERISON_ResetParseState();
}

/* 查询是否存在新的MAIXCAM数据 */
uint8_t* DRIVER_VERISON_HasNewData(void){
  return &verisonInfo.hasNewData;
}

/* 获取最近一次解析成功的MAIXCAM数据 */
uint8_t DRIVER_VERISON_GetMaixCamInfo(void){
  if(verisonInfo.hasNewData == 0U) return 0U;

  verisonInfo = verisonInfoCache;
  memcpy(verisonInfo.realData.raw, verisonInfo.data, 10);
  return 1U;
}
