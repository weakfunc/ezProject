/*============================================================================
 * MAIXCAM串口协议（固定16字节）
 * [0]     0x55        包头1
 * [1]     0xAA        包头2
 * [2]     0x01        控制字段
 * [3]     obj1        分类1编码（无目标/冷却中=0xFF）
 * [4]     obj2        分类2编码（无目标/冷却中=0xFF）
 * [5]     fps         摄像头帧率（uint8）
 * [6~12]  0x00        保留
 * [13]    CRC8        0x00时直接通过，否则校验[0]~[12]（多项式0x07）
 * [14]    CNT         包计数（0x00~0xFF循环递增）
 * [15]    0xFF        包尾
 *============================================================================*/

#include "driver_verison.h"
#include <string.h>

/* MAIXCAM模块数据（对外暴露） */
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
/* 协议包尾 */
#define VERISON_FRAME_TAIL                          (0xFFU)
/* CRC8初始值 */
#define VERISON_CRC8_INIT                           (0x00U)
/* 数据段长度（字节[3]~[12]共10字节） */
#define VERISON_DATA_LEN                            (10U)

/* 内部帧缓存，由USART回调函数写入 */
typedef struct {
  uint8_t  obj1;         /* 分类1编码 */
  uint8_t  obj2;         /* 分类2编码 */
  uint8_t  fps;          /* 摄像头帧率 */
  uint8_t  camCnt;       /* 摄像头CNT值 */
  uint8_t  hasValidData; /* 有效数据标志（obj1 != 0xFF） */
  uint8_t  hasNewData;   /* 新帧到达标志，Updata读取后清零 */
} verisonFrameCache_t;

/* MAIXCAM解析状态机枚举 */
typedef enum {
  VERISON_STATE_WAIT_SOF1 = 0U,
  VERISON_STATE_WAIT_SOF2,
  VERISON_STATE_WAIT_CTRL,
  VERISON_STATE_RECV_DATA,
  VERISON_STATE_WAIT_CRC8,
  VERISON_STATE_WAIT_CNT,
  VERISON_STATE_WAIT_TAIL,
} verisonParseState_e;

/* 帧解析完成后的内部缓存 */
static verisonFrameCache_t verisonFrameCache;
/* 摄像头数据帧总接收计数（CRC通过即计） */
static uint32_t verisonRxTotalCnt = 0U;
/* STM32本地有效包计数（仅obj1 != 0xFF时累加） */
static uint32_t verisonLocalPktCnt = 0U;
/* 当前解析状态 */
static verisonParseState_e verisonParseState = VERISON_STATE_WAIT_SOF1;
/* 数据段接收计数 */
static uint8_t verisonDataIdx = 0U;
/* 当前帧临时数据段 */
static uint8_t verisonDataTmp[VERISON_DATA_LEN];
/* 当前帧临时CRC8接收值 */
static uint8_t verisonCrc8Tmp = 0U;
/* 当前帧临时CNT值 */
static uint8_t verisonCntTmp = 0U;
/* 当前帧CRC8累计计算值 */
static uint8_t verisonCrc8Calc = VERISON_CRC8_INIT;
/* 当前帧CRC8校验通过标志 */
static uint8_t verisonCrc8PassFlag = 0U;

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

/* 重置解析状态机至初始状态 */
static void __DRIVER_VERISON_ResetParseState(void){
  verisonParseState = VERISON_STATE_WAIT_SOF1;
  verisonDataIdx    = 0U;
  verisonCrc8Tmp    = 0U;
  verisonCntTmp     = 0U;
  verisonCrc8Calc   = VERISON_CRC8_INIT;
  verisonCrc8PassFlag = 0U;
}

/* 将解析完成的帧数据写入内部缓存 */
static void __DRIVER_VERISON_SaveFrame(void){
  uint8_t isValid;

  /* 从数据段中提取各字段 */
  isValid = (verisonDataTmp[0] != VERISON_INVALID_OBJ) ? 1U : 0U;

  verisonFrameCache.obj1         = verisonDataTmp[0];
  verisonFrameCache.obj2         = verisonDataTmp[1];
  verisonFrameCache.fps          = verisonDataTmp[2];
  verisonFrameCache.camCnt       = verisonCntTmp;
  verisonFrameCache.hasValidData = isValid;
  verisonFrameCache.hasNewData   = 1U;

  /* CRC通过即累加总接收计数，仅obj1有效时才累加本地包计数 */
  verisonRxTotalCnt++;
  if(isValid != 0U){
    verisonLocalPktCnt++;
  }
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
      if(verisonCrc8PassFlag != 0U){
        __DRIVER_VERISON_SaveFrame();
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
  memset(&verisonFrameCache, 0, sizeof(verisonFrameCache));
  memset(&verisonInfo, 0, sizeof(verisonInfo));
  memset(verisonDataTmp, 0, sizeof(verisonDataTmp));
  verisonRxTotalCnt  = 0U;
  verisonLocalPktCnt = 0U;
  __DRIVER_VERISON_ResetParseState();
  VERISON_DEP_UART_SET_CUSTOM_CB(__DRIVER_VERISON_UartByteCallback);
}

/* 更新verisonInfo，读取内部缓存中最新帧数据，在userTask中10ms周期调用 */
void DRIVER_VERISON_Updata(void){
  if(verisonFrameCache.hasNewData != 0U){
    verisonInfo.obj1         = verisonFrameCache.obj1;
    verisonInfo.obj2         = verisonFrameCache.obj2;
    verisonInfo.fps          = verisonFrameCache.fps;
    verisonInfo.camCnt       = verisonFrameCache.camCnt;
    verisonInfo.hasValidData = verisonFrameCache.hasValidData;
    verisonInfo.hasNewData   = 1U;
    verisonFrameCache.hasNewData = 0U;
  } else {
    verisonInfo.hasNewData = 0U;
  }
  verisonInfo.rxTotalCnt  = verisonRxTotalCnt;
  verisonInfo.localPktCnt = verisonLocalPktCnt;
}
