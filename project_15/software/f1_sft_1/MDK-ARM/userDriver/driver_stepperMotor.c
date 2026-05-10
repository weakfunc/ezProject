/*============================================================================
 * README
 * 步进电机驱动模块（ZDT X42S 第二代闭环步进电机）
 * 通讯接口：USART2，波特率 115200，TTL 电平
 * 必须调用：
 * DRIVER_STEPPER_Update(); 电机状态更新，2msTask
 * Emm固件：读取 0x43 全部参数，并在弱钩子中发送控制命令
 *
 * 对步进电机的闭环控制必须通过以下弱函数实现，要不会有时序错误。
 * __weak void DRIVER_STEPPER_Axis0CtrlHook(void) {
      //0x01 电机控制函数
  }
  __weak void DRIVER_STEPPER_Axis1CtrlHook(void) {
     //0x02 电机控制函数
  }
  __weak void DRIVER_STEPPER_CommonCtrlHook(void) {
     //通用预留功能函数
  }

 * DRIVER_STEPPER_Init(); 电机初始化
 *============================================================================*/

#include "driver_stepperMotor.h"
#include <string.h>

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* 接收缓冲区大小（Emm 读取系统状态参数 0x43：功能码后 29 字节）。 */
#define STEPPER_RX_BUF_SIZE   32U

/* 发送帧最大长度。Emm固件位置模式最长 13 字节。 */
#define STEPPER_TX_BUF_SIZE   16U

/* Emm 读取系统状态参数回包总长 0x1F，除去地址和功能码后还需接收 29 字节。 */
#define STEPPER_EMM_SYS_PARAM_TOTAL_LEN  0x1FU
#define STEPPER_EMM_SYS_PARAM_RX_LEN     (STEPPER_EMM_SYS_PARAM_TOTAL_LEN - 2U)

/* 板端使能/回零内部命令序列。 */
#define STEPPER_BOARD_SEQ_NONE        0U
#define STEPPER_BOARD_SEQ_EN_PITCH    1U
#define STEPPER_BOARD_SEQ_EN_YAW      2U
#define STEPPER_BOARD_SEQ_HOME_PITCH  3U
#define STEPPER_BOARD_SEQ_HOME_YAW    4U
#define STEPPER_BOARD_SEQ_DIS_PITCH   5U
#define STEPPER_BOARD_SEQ_DIS_YAW     6U

/* 保存全部电机零点内部命令序列。 */
#define STEPPER_SAVE_ZERO_SEQ_NONE    0U
#define STEPPER_SAVE_ZERO_SEQ_PITCH   1U
#define STEPPER_SAVE_ZERO_SEQ_YAW     2U

#define STEPPER_AXIS_MASK(axisIdx)    ((uint8_t)(1U << (axisIdx)))

/*============================================================================
 * 私有类型定义
 *============================================================================*/

/* 接收状态机状态。 */
typedef enum {
  STEPPER_PARSE_WAIT_ADDR = 0,  /* 等待地址字节 */
  STEPPER_PARSE_WAIT_FUNC,      /* 等待功能码 */
  STEPPER_PARSE_WAIT_DATA,      /* 等待数据字节 */
} stepperParseState_e;

/* 接收状态机上下文（两轴共用同一串口，统一解析）。 */
typedef struct {
  stepperParseState_e state;
  uint8_t  axisIdx;                      /* 当前正在解析的轴在数组中的下标 */
  uint8_t  funcCode;                     /* 当前功能码 */
  uint8_t  rxBuf[STEPPER_RX_BUF_SIZE];  /* 功能码后的数据缓冲 */
  uint8_t  rxCnt;                        /* 已收到的字节数 */
  uint8_t  expectedCnt;                  /* 功能码后需接收的字节总数（含校验）*/
} stepperParseCtx_t;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 步进电机反馈数据数组（公有，供上层通过 STEPPER_INFO 宏访问）。 */
stepperMotorInfo_t stepperMotorInfo[STEPPER_MOTOR_CNT];

/* 接收解析上下文。 */
static stepperParseCtx_t sParseCtx;

/* 发送缓冲区（查询帧使用，控制帧在各控制函数局部组帧后直接发送）。 */
static uint8_t sTxBuf[STEPPER_TX_BUF_SIZE];

static uint8_t sUpdateStep;
static uint8_t sBoardCtrlSeq;
static uint8_t sSaveHomeZeroSeq;
static uint8_t sBoardEnabledTarget;
static uint8_t sMotorControlEnabled;
static uint8_t sHomeActiveMask;

/*============================================================================
 * 私有函数
 *============================================================================*/

static uint16_t __STEPPER_ReadU16BE(const uint8_t *data) {
  return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t __STEPPER_ReadU32BE(const uint8_t *data) {
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] <<  8) |  (uint32_t)data[3];
}

static void __STEPPER_EmmSetSignedPos(uint8_t sign, uint32_t raw,
                                      float *posRevs, float *posAngle_deg) {
  float revs  = (float)raw / 65536.0f;
  float angle = ((float)(raw % 65536U) * 360.0f) / 65536.0f;

  if(sign == 0x00U) {
    *posRevs = revs;
    *posAngle_deg = angle;
  } else {
    *posRevs = -revs;
    *posAngle_deg = (angle == 0.0f) ? 0.0f : (360.0f - angle);
  }
}

static int32_t __STEPPER_EmmRawTo001Deg(uint32_t raw) {
  uint64_t angle001deg = ((uint64_t)raw * 36000ULL + 32768ULL) / 65536ULL;

  if(angle001deg > 0x7FFFFFFFULL) {
    return (int32_t)0x7FFFFFFF;
  }
  return (int32_t)angle001deg;
}

static void __STEPPER_UpdateControlEnableState(void) {
  if((sBoardEnabledTarget != 0U) &&
     (sBoardCtrlSeq == STEPPER_BOARD_SEQ_NONE) &&
     (sHomeActiveMask == 0U)) {
    sMotorControlEnabled = 1U;
  } else {
    sMotorControlEnabled = 0U;
  }
}

static void __STEPPER_SaveHomeZeroSnapshot(uint8_t motorId) {
  stepperMotorInfo_t *info;

  if((motorId == 0U) || (motorId > STEPPER_MOTOR_CNT)) {
    return;
  }

  info = &STEPPER_INFO(motorId);
  info->homeZeroAngle_deg = info->fbdPosAngle_deg;
  info->homeZeroEncoder = info->encoder;
}

/* 根据功能码返回其后需接收的字节总数（含末尾校验字节 0x6B）。 */
static uint8_t __STEPPER_GetExpectedDataLen(uint8_t funcCode) {
  switch(funcCode) {
    /* 实时位置 / 位置误差：符号(1)+4字节值+0x6B = 6 */
    case 0x36U: case 0x37U: return 6U;
    /* 实时转速：符号(1)+2字节值+0x6B = 4 */
    case 0x35U:             return 4U;
    /* 线性化编码器 / 总线电压 / 相电流 / 驱动温度 / 回零+电机标志：2字节值+0x6B = 3 */
    case 0x31U: case 0x26U: case 0x27U: case 0x39U: case 0x3CU: return 3U;
    /* Emm 读取系统状态参数：总长 31 字节，地址和功能码之后还有 29 字节。 */
    case 0x43U:             return STEPPER_EMM_SYS_PARAM_RX_LEN;
    /* 其余命令（ACK类 / 单字节标志）：1字节+0x6B = 2 */
    default:                return 2U;
  }
}

/* 解析已接收的完整响应，更新对应轴的 stepperMotorInfo 结构体。
 * data: rxBuf 起始地址，data[expectedCnt-1] 为校验字节（不使用）。
 */
static void __STEPPER_ParseResponse(uint8_t axisIdx, uint8_t funcCode,
                                    const uint8_t *data) {
  stepperMotorInfo_t *info = &stepperMotorInfo[axisIdx];
  uint32_t rawVal;
  uint16_t rawU16;
  float fbdPosRevs;
  float fbdPosAngle_deg;
  uint32_t refRaw;
  uint32_t fbdRaw;
  uint32_t errRaw;
  int32_t signed001deg;

  switch(funcCode) {
    case 0x36U:  /* 读取实时位置：data[0]=符号, data[1..4]=原始实时位置 */
      rawVal = __STEPPER_ReadU32BE(&data[1]);
      /* Emm固件：65536 个原始计数表示一圈。 */
      __STEPPER_EmmSetSignedPos(data[0], rawVal, &fbdPosRevs, &fbdPosAngle_deg);
      info->fbdPosSign = data[0];
      info->fbdPosRaw = rawVal;
      info->fbdPosRevs = fbdPosRevs;
      info->fbdPosAngle_deg = fbdPosAngle_deg;
      break;
    case 0x35U:  /* 读取实时转速：data[0]=符号, data[1..2]=转速(RPM) */
      rawVal = __STEPPER_ReadU16BE(&data[1]);
      info->fbdSpd_rpm = (int16_t)((data[0] == 0x00U) ? (int32_t)rawVal : -(int32_t)rawVal);
      break;
    case 0x31U:  /* 读取线性化编码器值：data[0..1]=编码器值 */
      info->encoder = __STEPPER_ReadU16BE(&data[0]);
      break;
    case 0x37U:  /* 读取位置误差：data[0]=符号, data[1..4]=误差(0.01°) */
      rawVal = __STEPPER_ReadU32BE(&data[1]);
      info->posErr_001deg = (data[0] == 0x00U) ? (int32_t)rawVal : -(int32_t)rawVal;
      break;
    case 0x26U:  /* 读取总线电压：data[0..1]=电压(mV)，存储单位 V */
      info->busCurrent = (float)__STEPPER_ReadU16BE(&data[0]) / 1000.0f;
      break;
    case 0x27U:  /* 读取相电流：data[0..1]=相电流(mA) */
      rawU16 = __STEPPER_ReadU16BE(&data[0]);
      info->phaseCurrent_A = (float)rawU16 / 1000.0f;
      break;
    case 0x39U:  /* 读取驱动温度：data[0]=符号, data[1]=温度 */
      info->driverTemp = (data[0] == 0x00U) ? (int16_t)data[1] : -(int16_t)data[1];
      break;
    case 0x3CU:  /* 读取回零+电机标志：data[0]=回零标志, data[1]=电机标志 */
      info->homeStatus  = data[0];
      info->motorStatus = data[1];
      break;
    case 0x43U:  /* Emm 读取系统状态参数：31 字节完整状态包 */
      info->busCurrent = (float)__STEPPER_ReadU16BE(&data[2]) / 1000.0f;
      info->phaseCurrent_A = (float)__STEPPER_ReadU16BE(&data[4]) / 1000.0f;
      info->encoder = __STEPPER_ReadU16BE(&data[6]);

      refRaw = __STEPPER_ReadU32BE(&data[9]);
      info->refPosSign = data[8];
      info->refPosRaw = refRaw;
      __STEPPER_EmmSetSignedPos(info->refPosSign, refRaw,
                                &info->refPosRevs,
                                &info->refPosAngle_deg);

      rawU16 = __STEPPER_ReadU16BE(&data[14]);
      info->fbdSpd_rpm = (int16_t)((data[13] == 0x00U) ? (int32_t)rawU16 : -(int32_t)rawU16);

      fbdRaw = __STEPPER_ReadU32BE(&data[17]);
      info->fbdPosSign = data[16];
      info->fbdPosRaw = fbdRaw;
      __STEPPER_EmmSetSignedPos(info->fbdPosSign, fbdRaw,
                                &info->fbdPosRevs,
                                &info->fbdPosAngle_deg);

      errRaw = __STEPPER_ReadU32BE(&data[22]);
      info->posErrSign = data[21];
      info->posErrRaw = errRaw;
      info->posErrAngle_deg = ((float)errRaw * 360.0f) / 65536.0f;
      signed001deg = __STEPPER_EmmRawTo001Deg(errRaw);
      if(info->posErrSign != 0x00U) {
        info->posErrAngle_deg = -info->posErrAngle_deg;
        signed001deg = -signed001deg;
      }
      info->posErr_001deg = signed001deg;

      info->homeStatus = data[26];
      info->motorStatus = data[27];
      break;
    case 0x9AU:  /* 触发回零应答：0x02=接收, 0x9F=完成, 0xE2/0xEE=错误 */
      info->lastAck = data[0];
      if((data[0] == STEPPER_ACK_DONE) ||
         (data[0] == STEPPER_ACK_ERR_PARAM) ||
         (data[0] == STEPPER_ACK_ERR_FMT)) {
        sHomeActiveMask &= (uint8_t)~STEPPER_AXIS_MASK(axisIdx);
        __STEPPER_UpdateControlEnableState();
      }
      break;
    default:     /* 其余命令 ACK 应答：data[0]=应答码 */
      info->lastAck = data[0];
      break;
  }
}

/* USART2 自定义字节接收回调（中断上下文）。
 * 驱动接收状态机：等待地址→等待功能码→收集数据→解析。
 */
static void __STEPPER_RxByteCallback(uint8_t port, uint8_t byte) {
  uint8_t i;
  (void)port;

  switch(sParseCtx.state) {
    case STEPPER_PARSE_WAIT_ADDR:
      for(i = 0U; i < STEPPER_MOTOR_CNT; i++) {
        if(byte == stepperMotorInfo[i].addr) {
          sParseCtx.axisIdx = i;
          sParseCtx.state   = STEPPER_PARSE_WAIT_FUNC;
          break;
        }
      }
      break;

    case STEPPER_PARSE_WAIT_FUNC:
      sParseCtx.funcCode    = byte;
      sParseCtx.rxCnt       = 0U;
      sParseCtx.expectedCnt = __STEPPER_GetExpectedDataLen(byte);
      sParseCtx.state       = STEPPER_PARSE_WAIT_DATA;
      break;

    case STEPPER_PARSE_WAIT_DATA:
      if(sParseCtx.rxCnt < STEPPER_RX_BUF_SIZE) {
        sParseCtx.rxBuf[sParseCtx.rxCnt] = byte;
        sParseCtx.rxCnt++;
      }
      if(sParseCtx.rxCnt >= sParseCtx.expectedCnt) {
        /* 最后一字节为校验码 0x6B，实际数据在 rxBuf[0..expectedCnt-2] */
        __STEPPER_ParseResponse(sParseCtx.axisIdx, sParseCtx.funcCode,
                                sParseCtx.rxBuf);
        sParseCtx.state = STEPPER_PARSE_WAIT_ADDR;
      }
      break;

    default:
      sParseCtx.state = STEPPER_PARSE_WAIT_ADDR;
      break;
  }
}

static void __STEPPER_RunCtrlHook(void (*ctrlHook)(void)) {
  ctrlHook();
}

static uint8_t __STEPPER_RunBoardCtrlCommand(void) {
  switch(sBoardCtrlSeq) {
    case STEPPER_BOARD_SEQ_EN_PITCH:
      DRIVER_STEPPER_Enable(STEPPER_ADDR_PITCH, 1U);
      sBoardCtrlSeq = STEPPER_BOARD_SEQ_EN_YAW;
      break;
    case STEPPER_BOARD_SEQ_EN_YAW:
      DRIVER_STEPPER_Enable(STEPPER_ADDR_YAW, 1U);
      sBoardCtrlSeq = STEPPER_BOARD_SEQ_HOME_PITCH;
      break;
    case STEPPER_BOARD_SEQ_HOME_PITCH:
      DRIVER_STEPPER_GoHome(STEPPER_ADDR_PITCH, STEPPER_HOME_MODE_SINGLE_NEAREST);
      sHomeActiveMask |= STEPPER_AXIS_MASK(0U);
      sBoardCtrlSeq = STEPPER_BOARD_SEQ_HOME_YAW;
      break;
    case STEPPER_BOARD_SEQ_HOME_YAW:
      DRIVER_STEPPER_GoHome(STEPPER_ADDR_YAW, STEPPER_HOME_MODE_SINGLE_NEAREST);
      sHomeActiveMask |= STEPPER_AXIS_MASK(1U);
      sBoardCtrlSeq = STEPPER_BOARD_SEQ_NONE;
      __STEPPER_UpdateControlEnableState();
      break;
    case STEPPER_BOARD_SEQ_DIS_PITCH:
      DRIVER_STEPPER_Enable(STEPPER_ADDR_PITCH, 0U);
      sBoardCtrlSeq = STEPPER_BOARD_SEQ_DIS_YAW;
      break;
    case STEPPER_BOARD_SEQ_DIS_YAW:
      DRIVER_STEPPER_Enable(STEPPER_ADDR_YAW, 0U);
      sBoardCtrlSeq = STEPPER_BOARD_SEQ_NONE;
      __STEPPER_UpdateControlEnableState();
      break;
    default:
      sBoardCtrlSeq = STEPPER_BOARD_SEQ_NONE;
      return 0U;
  }

  return 1U;
}

static uint8_t __STEPPER_RunSaveHomeZeroCommand(void) {
  switch(sSaveHomeZeroSeq) {
    case STEPPER_SAVE_ZERO_SEQ_PITCH:
      __STEPPER_SaveHomeZeroSnapshot(STEPPER_ADDR_PITCH);
      DRIVER_STEPPER_SetHomeZero(STEPPER_ADDR_PITCH, 1U);
      sSaveHomeZeroSeq = STEPPER_SAVE_ZERO_SEQ_YAW;
      break;
    case STEPPER_SAVE_ZERO_SEQ_YAW:
      __STEPPER_SaveHomeZeroSnapshot(STEPPER_ADDR_YAW);
      DRIVER_STEPPER_SetHomeZero(STEPPER_ADDR_YAW, 1U);
      sSaveHomeZeroSeq = STEPPER_SAVE_ZERO_SEQ_NONE;
      break;
    default:
      sSaveHomeZeroSeq = STEPPER_SAVE_ZERO_SEQ_NONE;
      return 0U;
  }

  return 1U;
}

static uint8_t __STEPPER_RunInternalCommand(void) {
  if(__STEPPER_RunBoardCtrlCommand() != 0U) {
    return 1U;
  }

  return __STEPPER_RunSaveHomeZeroCommand();
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化步进电机驱动，配置两轴地址并注册 USART2 接收回调。 */
void DRIVER_STEPPER_Init(void) {
  uint8_t pendingBoardEnabledTarget = sBoardEnabledTarget;
  uint8_t pendingSaveHomeZeroSeq = sSaveHomeZeroSeq;

  memset(stepperMotorInfo, 0, sizeof(stepperMotorInfo));
  memset(&sParseCtx, 0, sizeof(sParseCtx));
  sUpdateStep = 0U;
  sBoardCtrlSeq = STEPPER_BOARD_SEQ_NONE;
  sSaveHomeZeroSeq = STEPPER_SAVE_ZERO_SEQ_NONE;
  sBoardEnabledTarget = 0U;
  sMotorControlEnabled = 0U;
  sHomeActiveMask = 0U;

  stepperMotorInfo[0].addr = STEPPER_ADDR_PITCH;
  stepperMotorInfo[1].addr = STEPPER_ADDR_YAW;
  sParseCtx.state = STEPPER_PARSE_WAIT_ADDR;

  STEPPER_DEP_UART_SET_CB(__STEPPER_RxByteCallback);

  DRIVER_STEPPER_Enable(STEPPER_ADDR_PITCH, 0U);
  DRIVER_STEPPER_Enable(STEPPER_ADDR_YAW,   0U);

  if(pendingSaveHomeZeroSeq != STEPPER_SAVE_ZERO_SEQ_NONE) {
    sSaveHomeZeroSeq = STEPPER_SAVE_ZERO_SEQ_PITCH;
  }

  if(pendingBoardEnabledTarget != 0U) {
    DRIVER_STEPPER_RequestBoardEnable(1U);
  }
}

/* 使能/失能电机（功能码 F3）。 */
void DRIVER_STEPPER_Enable(uint8_t motorId, uint8_t enable) {
  uint8_t txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t len = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0xF3U;
  txBuf[len++] = 0xABU;
  txBuf[len++] = (enable != 0U) ? 0x01U : 0x00U;
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

/* 立即停止电机（功能码 FE）。 */
void DRIVER_STEPPER_Stop(uint8_t motorId) {
  uint8_t txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t len = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0xFEU;
  txBuf[len++] = 0x98U;
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

/* 触发回零（功能码 9A）。 */
void DRIVER_STEPPER_GoHome(uint8_t motorId, uint8_t homeMode) {
  uint8_t txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t len = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0x9AU;
  txBuf[len++] = homeMode;
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

/* 设置单圈回零的零点位置（功能码 93）。 */
void DRIVER_STEPPER_SetHomeZero(uint8_t motorId, uint8_t saveFlag) {
  uint8_t txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t len = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0x93U;
  txBuf[len++] = 0x88U;
  txBuf[len++] = (saveFlag != 0U) ? 0x01U : 0x00U;
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

void DRIVER_STEPPER_RequestSaveAllHomeZero(void) {
  sSaveHomeZeroSeq = STEPPER_SAVE_ZERO_SEQ_PITCH;
}

void DRIVER_STEPPER_RequestBoardEnable(uint8_t enable) {
  if(enable != 0U) {
    sBoardEnabledTarget = 1U;
    sHomeActiveMask = 0U;
    sMotorControlEnabled = 0U;
    sBoardCtrlSeq = STEPPER_BOARD_SEQ_EN_PITCH;
  } else {
    sBoardEnabledTarget = 0U;
    sHomeActiveMask = 0U;
    sMotorControlEnabled = 0U;
    sBoardCtrlSeq = STEPPER_BOARD_SEQ_DIS_PITCH;
  }
}

/* 将当前位置清零（功能码 0A）。 */
void DRIVER_STEPPER_ZeroPos(uint8_t motorId) {
  uint8_t txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t len = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0x0AU;
  txBuf[len++] = 0x6DU;
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

/* 解除堵转/过热/过流保护（功能码 0E）。 */
void DRIVER_STEPPER_ClearError(uint8_t motorId) {
  uint8_t txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t len = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0x0EU;
  txBuf[len++] = 0x52U;
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

/* 发送读取实时位置请求（功能码 36）。 */
void DRIVER_STEPPER_ReadPos(uint8_t motorId) {
  uint8_t len = 0U;
  sTxBuf[len++] = motorId;
  sTxBuf[len++] = 0x36U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取实时转速请求（功能码 35）。 */
void DRIVER_STEPPER_ReadSpeed(uint8_t motorId) {
  uint8_t len = 0U;
  sTxBuf[len++] = motorId;
  sTxBuf[len++] = 0x35U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取线性化编码器值请求（功能码 31）。 */
void DRIVER_STEPPER_ReadEncoder(uint8_t motorId) {
  uint8_t len = 0U;
  sTxBuf[len++] = motorId;
  sTxBuf[len++] = 0x31U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取位置误差请求（功能码 37）。 */
void DRIVER_STEPPER_ReadPosError(uint8_t motorId) {
  uint8_t len = 0U;
  sTxBuf[len++] = motorId;
  sTxBuf[len++] = 0x37U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取总线电压请求（功能码 26）。 */
void DRIVER_STEPPER_ReadBusCurrent(uint8_t motorId) {
  uint8_t len = 0U;
  sTxBuf[len++] = motorId;
  sTxBuf[len++] = 0x26U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取相电流请求（功能码 27）。 */
void DRIVER_STEPPER_ReadPhaseCurrent(uint8_t motorId) {
  uint8_t len = 0U;
  sTxBuf[len++] = motorId;
  sTxBuf[len++] = 0x27U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取驱动温度请求（功能码 39）。 */
void DRIVER_STEPPER_ReadDriverTemp(uint8_t motorId) {
  uint8_t len = 0U;
  sTxBuf[len++] = motorId;
  sTxBuf[len++] = 0x39U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 读取系统状态参数（功能码 43，Emm固件）。 */
void DRIVER_STEPPER_ReadAllParams(uint8_t motorId) {
  uint8_t len = 0U;
  sTxBuf[len++] = motorId;
  sTxBuf[len++] = 0x43U;
  sTxBuf[len++] = 0x7AU;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

__weak void DRIVER_STEPPER_Axis0CtrlHook(void) {
  //0x01 电机控制函数
}

__weak void DRIVER_STEPPER_Axis1CtrlHook(void) {
  //0x02 电机控制函数
}

__weak void DRIVER_STEPPER_CommonCtrlHook(void) {
  //通用预留功能函数
}

/* 参数查询/控制发送状态机（2ms 任务周期调用）。
 * 0=轴0全部参数，1=轴0控制钩子，2=轴1全部参数，
 * 3=轴1控制钩子，4=通用预留功能钩子。
 * 电机实际控制通过重写 控制弱函数实现 要不会有时序问题
 */
void DRIVER_STEPPER_Update(void) {
  if(__STEPPER_RunInternalCommand() != 0U) {
    return;
  }

  switch(sUpdateStep) {
    case 0U:
      DRIVER_STEPPER_ReadAllParams(stepperMotorInfo[0].addr);
      break;
    case 1U:
      //0x01 电机控制函数
      if(sMotorControlEnabled != 0U) {
        __STEPPER_RunCtrlHook(DRIVER_STEPPER_Axis0CtrlHook);
      }
      break;
    case 2U:
      DRIVER_STEPPER_ReadAllParams(stepperMotorInfo[1].addr);
      break;
    case 3U:
      //0x02 电机控制函数
      if(sMotorControlEnabled != 0U) {
        __STEPPER_RunCtrlHook(DRIVER_STEPPER_Axis1CtrlHook);
      }
      break;
    case 4U:
      __STEPPER_RunCtrlHook(DRIVER_STEPPER_CommonCtrlHook);
      break;
    default:
      sUpdateStep = 0U;
      DRIVER_STEPPER_ReadAllParams(stepperMotorInfo[0].addr);
      break;
  }

  sUpdateStep++;
  if(sUpdateStep >= 5U) {
    sUpdateStep = 0U;
  }
}

/* 转动指定圈数（相对当前位置）。 */
void DRIVER_STEPPER_RotateRevs(uint8_t motorId, int16_t speed, uint8_t acc, uint32_t revs) {
  uint8_t  dir      = (speed < 0) ? 0x01U : 0x00U;
  uint16_t absSpeed = (speed < 0) ? (uint16_t)(-speed) : (uint16_t)speed;
  uint8_t  txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t  len      = 0U;
  /* Emm固件：使用位置模式（功能码 FD），脉冲数，vel 单位 RPM。 */
  uint32_t clk = revs * STEPPER_PULSE_PER_REV;
  txBuf[len++] = motorId;
  txBuf[len++] = 0xFDU;
  txBuf[len++] = dir;
  txBuf[len++] = (uint8_t)(absSpeed >> 8);
  txBuf[len++] = (uint8_t)(absSpeed & 0xFFU);
  txBuf[len++] = acc;
  txBuf[len++] = (uint8_t)(clk >> 24);
  txBuf[len++] = (uint8_t)(clk >> 16);
  txBuf[len++] = (uint8_t)(clk >>  8);
  txBuf[len++] = (uint8_t)(clk & 0xFFU);
  txBuf[len++] = STEPPER_MODE_REL_CUR;
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */

  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

/* 速度模式控制（功能码 F6，Emm固件）。 */
void DRIVER_STEPPER_SetSpeed(uint8_t motorId, int16_t vel, uint8_t acc) {
  uint8_t  dir     = (vel < 0) ? 0x01U : 0x00U;
  uint16_t absVel  = (vel < 0) ? (uint16_t)(-vel) : (uint16_t)vel;
  uint8_t  txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t  len     = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0xF6U;
  txBuf[len++] = dir;
  txBuf[len++] = (uint8_t)(absVel >> 8);
  txBuf[len++] = (uint8_t)(absVel & 0xFFU);
  txBuf[len++] = acc;
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

/* 位置模式控制（功能码 FD，Emm固件）。 */
void DRIVER_STEPPER_SetPos(uint8_t motorId, int16_t vel, uint8_t acc,
                           uint32_t clk, uint8_t mode) {
  uint8_t  dir    = (vel < 0) ? 0x01U : 0x00U;
  uint16_t absVel = (vel < 0) ? (uint16_t)(-vel) : (uint16_t)vel;
  uint8_t  txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t  len    = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0xFDU;
  txBuf[len++] = dir;
  txBuf[len++] = (uint8_t)(absVel >> 8);
  txBuf[len++] = (uint8_t)(absVel & 0xFFU);
  txBuf[len++] = acc;
  txBuf[len++] = (uint8_t)(clk >> 24);
  txBuf[len++] = (uint8_t)(clk >> 16);
  txBuf[len++] = (uint8_t)(clk >>  8);
  txBuf[len++] = (uint8_t)(clk & 0xFFU);
  txBuf[len++] = mode;
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}
