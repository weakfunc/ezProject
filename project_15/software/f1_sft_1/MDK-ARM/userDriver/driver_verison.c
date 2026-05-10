#include "driver_verison.h"
#include <string.h>

verisonInfo_t verisonInfo;

#define VERISON_FRAME_SOF1                0x55U
#define VERISON_FRAME_SOF2                0xAAU
#define VERISON_FRAME_RX_CTRL             0x01U
#define VERISON_FRAME_TX_CTRL             0x00U
#define VERISON_FRAME_TAIL                0xFFU
#define VERISON_CRC8_INIT                 0x00U
#define VERISON_CRC8_DISABLED             0x00U
#define VERISON_FRAME_LEN                 FRAME_LEN

/* 摄像头正常 10ms 发一帧，连续 100ms 无帧认为离线。 */
#define VERISON_CONNECT_TIMEOUT_10MS      10U
#define VERISON_INVALID_VALUE             0xFFU
#define VERISON_YAW_VALID_FLAG_IDX        8U
#define VERISON_PITCH_VALID_FLAG_IDX      9U

typedef enum {
  VERISON_STATE_WAIT_SOF1 = 0U,
  VERISON_STATE_WAIT_SOF2,
  VERISON_STATE_WAIT_CTRL,
  VERISON_STATE_RECV_DATA,
  VERISON_STATE_WAIT_CRC8,
  VERISON_STATE_WAIT_CNT,
  VERISON_STATE_WAIT_TAIL,
} verisonParseState_e;

static verisonParseState_e verisonParseState = VERISON_STATE_WAIT_SOF1;
static verisonInfo_t verisonInfoCache;
static uint8_t verisonDataTmp[VERISON_DATA_LEN];
static uint8_t verisonDataIdx;
static uint8_t verisonCtrlTmp;
static uint8_t verisonCrc8Tmp;
static uint8_t verisonCntTmp;
static uint8_t verisonCrc8Calc = VERISON_CRC8_INIT;
static uint8_t verisonCrc8PassFlag;
static uint8_t verisonTxCnt;
static uint8_t verisonConnectTimeoutCnt;
static uint32_t verisonCrcErrTotalCnt;
static uint32_t verisonRxTotalCnt;

static uint8_t __DRIVER_VERISON_Crc8Calc(uint8_t crc, uint8_t byte) {
  uint8_t i;

  crc ^= byte;
  for(i = 0U; i < 8U; i++) {
    if((crc & 0x80U) != 0U) {
      crc = (uint8_t)((crc << 1U) ^ 0x07U);
    } else {
      crc = (uint8_t)(crc << 1U);
    }
  }
  return crc;
}

static float __DRIVER_VERISON_ReadFloatLE(const uint8_t *data) {
  float value;

  memcpy(&value, data, sizeof(value));
  return value;
}

static uint8_t __DRIVER_VERISON_IsAllInvalidByte(const uint8_t *data,
                                                 uint8_t len) {
  uint8_t i;

  for(i = 0U; i < len; i++) {
    if(data[i] != VERISON_INVALID_VALUE) {
      return 0U;
    }
  }
  return 1U;
}

static uint8_t __DRIVER_VERISON_IsAngleInvalid(const uint8_t *angleData,
                                               uint8_t validFlag) {
  if(validFlag == VERISON_INVALID_VALUE) {
    return 1U;
  }

  return __DRIVER_VERISON_IsAllInvalidByte(angleData, sizeof(float));
}

static void __DRIVER_VERISON_SyncTotalCnt(void) {
  verisonInfoCache.crcErrTotalCnt = verisonCrcErrTotalCnt;
  verisonInfoCache.rxTotalCnt = verisonRxTotalCnt;
  verisonInfo.crcErrTotalCnt = verisonCrcErrTotalCnt;
  verisonInfo.rxTotalCnt = verisonRxTotalCnt;
}

static void __DRIVER_VERISON_SaveFrame(void) {
  uint8_t i;

  verisonInfoCache.ctrl = verisonCtrlTmp;
  verisonInfoCache.crc8 = verisonCrc8Tmp;
  verisonInfoCache.cnt = verisonCntTmp;
  verisonInfoCache.hasNewData = 1U;
  verisonInfoCache.isConnect = 1U;
  for(i = 0U; i < VERISON_DATA_LEN; i++) {
    verisonInfoCache.data[i] = verisonDataTmp[i];
  }

  __DRIVER_VERISON_SyncTotalCnt();
  verisonInfo = verisonInfoCache;
}

static void __DRIVER_VERISON_ResetParseState(void) {
  verisonParseState = VERISON_STATE_WAIT_SOF1;
  verisonDataIdx = 0U;
  verisonCtrlTmp = 0U;
  verisonCrc8Tmp = 0U;
  verisonCntTmp = 0U;
  verisonCrc8Calc = VERISON_CRC8_INIT;
  verisonCrc8PassFlag = 0U;
}

static void __DRIVER_VERISON_UartByteCallback(uint8_t port, uint8_t byte) {
  if(port != VERISON_DEP_UART_PORT) {
    return;
  }

  switch(verisonParseState) {
    case VERISON_STATE_WAIT_SOF1:
      if(byte == VERISON_FRAME_SOF1) {
        verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(VERISON_CRC8_INIT, byte);
        verisonParseState = VERISON_STATE_WAIT_SOF2;
      }
      break;

    case VERISON_STATE_WAIT_SOF2:
      if(byte == VERISON_FRAME_SOF2) {
        verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(verisonCrc8Calc, byte);
        verisonParseState = VERISON_STATE_WAIT_CTRL;
      } else {
        __DRIVER_VERISON_ResetParseState();
        if(byte == VERISON_FRAME_SOF1) {
          verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(VERISON_CRC8_INIT, byte);
          verisonParseState = VERISON_STATE_WAIT_SOF2;
        }
      }
      break;

    case VERISON_STATE_WAIT_CTRL:
      if(byte == VERISON_FRAME_RX_CTRL) {
        verisonCtrlTmp = byte;
        memset(verisonDataTmp, 0, sizeof(verisonDataTmp));
        verisonDataIdx = 0U;
        verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(verisonCrc8Calc, byte);
        verisonParseState = VERISON_STATE_RECV_DATA;
      } else {
        __DRIVER_VERISON_ResetParseState();
        if(byte == VERISON_FRAME_SOF1) {
          verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(VERISON_CRC8_INIT, byte);
          verisonParseState = VERISON_STATE_WAIT_SOF2;
        }
      }
      break;

    case VERISON_STATE_RECV_DATA:
      verisonDataTmp[verisonDataIdx] = byte;
      verisonDataIdx++;
      verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(verisonCrc8Calc, byte);
      if(verisonDataIdx >= VERISON_DATA_LEN) {
        verisonParseState = VERISON_STATE_WAIT_CRC8;
      }
      break;

    case VERISON_STATE_WAIT_CRC8:
      verisonCrc8Tmp = byte;
      if(byte == VERISON_CRC8_DISABLED) {
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
      if(byte == VERISON_FRAME_TAIL) {
        verisonRxTotalCnt++;
        if(verisonCrc8PassFlag != 0U) {
          verisonConnectTimeoutCnt = 0U;
          __DRIVER_VERISON_SaveFrame();
        } else {
          verisonCrcErrTotalCnt++;
          __DRIVER_VERISON_SyncTotalCnt();
        }
      }
      __DRIVER_VERISON_ResetParseState();
      if(byte == VERISON_FRAME_SOF1) {
        verisonCrc8Calc = __DRIVER_VERISON_Crc8Calc(VERISON_CRC8_INIT, byte);
        verisonParseState = VERISON_STATE_WAIT_SOF2;
      }
      break;

    default:
      __DRIVER_VERISON_ResetParseState();
      break;
  }
}

void DRIVER_VERISON_Init(void) {
  DRIVER_VERISON_Reset();
  VERISON_DEP_UART_SET_CUSTOM_CB(__DRIVER_VERISON_UartByteCallback);
}

void DRIVER_VERISON_Reset(void) {
  memset(&verisonInfoCache, 0, sizeof(verisonInfoCache));
  memset(&verisonInfo, 0, sizeof(verisonInfo));
  memset(verisonDataTmp, 0, sizeof(verisonDataTmp));
  verisonCrcErrTotalCnt = 0U;
  verisonRxTotalCnt = 0U;
  verisonTxCnt = 0U;
  verisonConnectTimeoutCnt = VERISON_CONNECT_TIMEOUT_10MS;
  __DRIVER_VERISON_ResetParseState();
}

uint8_t* DRIVER_VERISON_HasNewData(void) {
  return &verisonInfo.hasNewData;
}

uint8_t DRIVER_VERISON_Updata(void) {
  float lastYaw_deg;
  float lastPitch_deg;
  uint8_t yawInvalid;
  uint8_t pitchInvalid;

  if(verisonInfo.hasNewData == 0U) {
    if(verisonConnectTimeoutCnt < VERISON_CONNECT_TIMEOUT_10MS) {
      verisonConnectTimeoutCnt++;
    }
    if(verisonConnectTimeoutCnt >= VERISON_CONNECT_TIMEOUT_10MS) {
      verisonInfo.isConnect = 0U;
      verisonInfoCache.isConnect = 0U;
    }
    return 0U;
  }

  lastYaw_deg = verisonInfo.yaw_deg;
  lastPitch_deg = verisonInfo.pitch_deg;

  verisonInfo = verisonInfoCache;
  memcpy(verisonInfo.realData.raw, verisonInfo.data, VERISON_DATA_LEN);

  /* 摄像头丢失目标时会发 0xFF，无效轴保持上一次有效期望值。 */
  yawInvalid = __DRIVER_VERISON_IsAngleInvalid(&verisonInfo.data[0],
                                               verisonInfo.data[VERISON_YAW_VALID_FLAG_IDX]);
  pitchInvalid = __DRIVER_VERISON_IsAngleInvalid(&verisonInfo.data[4],
                                                 verisonInfo.data[VERISON_PITCH_VALID_FLAG_IDX]);

  if(yawInvalid == 0U) {
    verisonInfo.yaw_deg = __DRIVER_VERISON_ReadFloatLE(&verisonInfo.data[0]);
  } else {
    verisonInfo.yaw_deg = lastYaw_deg;
  }

  if(pitchInvalid == 0U) {
    verisonInfo.pitch_deg = __DRIVER_VERISON_ReadFloatLE(&verisonInfo.data[4]);
  } else {
    verisonInfo.pitch_deg = lastPitch_deg;
  }

  verisonInfo.hasNewData = 0U;
  verisonInfo.isConnect = 1U;
  verisonInfoCache.hasNewData = 0U;
  verisonInfoCache.isConnect = 1U;
  verisonInfoCache.yaw_deg = verisonInfo.yaw_deg;
  verisonInfoCache.pitch_deg = verisonInfo.pitch_deg;
  verisonConnectTimeoutCnt = 0U;
  return 1U;
}

uint8_t DRIVER_VERISON_SendData(const uint8_t *data, uint8_t len) {
  uint8_t i;
  uint8_t copyLen;
  uint8_t txBuf[VERISON_FRAME_LEN];

  if((data == NULL) && (len != 0U)) {
    return 0U;
  }

  memset(txBuf, 0, sizeof(txBuf));
  txBuf[0] = VERISON_FRAME_SOF1;
  txBuf[1] = VERISON_FRAME_SOF2;
  txBuf[2] = VERISON_FRAME_TX_CTRL;

  copyLen = (len > VERISON_DATA_LEN) ? VERISON_DATA_LEN : len;
  for(i = 0U; i < copyLen; i++) {
    txBuf[3U + i] = data[i];
  }

  txBuf[13] = VERISON_CRC8_DISABLED;
  txBuf[14] = verisonTxCnt++;
  txBuf[15] = VERISON_FRAME_TAIL;
  VERISON_DEP_UART_SEND_RAW(txBuf, VERISON_FRAME_LEN);
  return 1U;
}
