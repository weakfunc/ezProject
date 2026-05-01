/*============================================================================
 * README
 * 步进电机驱动模块（ZDT X42S 第二代闭环步进电机）
 * 通讯接口：USART2，波特率 115200，TTL 电平
 * 必须调用：
 * DRIVER_STEPPER_Update(); 电机状态更新，2msTask（前4步查询，后2步执行弱钩子）
 * 该函数实际控制周期为10ms
 *
 * 对步进电机的闭环控制必须通过以下弱函数实现，要不会有时序错误。
 * __weak void DRIVER_STEPPER_Axis0CtrlHook(void) {
      //0x01 电机控制函数
  }
  __weak void DRIVER_STEPPER_Axis1CtrlHook(void) {
     //0x02 电机控制函数
  }

 * DRIVER_STEPPER_Init(); 电机初始化
 *============================================================================*/

#include "driver_stepperMotor.h"
#include <string.h>

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* 接收缓冲区大小（功能码后最多 6 字节数据 + 1 字节校验）。 */
#define STEPPER_RX_BUF_SIZE   8U

/* 发送帧最大长度。X固件梯形位置模式最长 16 字节，Emm固件位置模式最长 13 字节。 */
#if defined(STEPPER_FIRMWARE_X)
#define STEPPER_TX_BUF_SIZE   20U
#else
#define STEPPER_TX_BUF_SIZE   16U
#endif

/* DRIVER_STEPPER_Update() 每 2ms 调用一次，50 次分频为 100ms。 */
#define STEPPER_STATUS_UPDATE_DIV    50U
#define STEPPER_STATUS_UPDATE_STEPS  3U

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
static uint8_t sStatusUpdateCnt;
static uint8_t sStatusUpdateStep;
static uint8_t sStatusAxisIdx;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 根据功能码返回其后需接收的字节总数（含末尾校验字节 0x6B）。 */
static uint8_t __STEPPER_GetExpectedDataLen(uint8_t funcCode) {
  switch(funcCode) {
    /* 实时位置 / 位置误差：符号(1)+4字节值+0x6B = 6 */
    case 0x36U: case 0x37U: return 6U;
    /* 实时转速：符号(1)+2字节值+0x6B = 4 */
    case 0x35U:             return 4U;
    /* 线性化编码器 / 总线电流 / 相电流 / 驱动温度 / 回零+电机标志：2字节值+0x6B = 3 */
    case 0x31U: case 0x26U: case 0x27U: case 0x39U: case 0x3CU: return 3U;
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
  float realPosRevs;
  float realPosAngle_deg;

  switch(funcCode) {
    case 0x36U:  /* 读取实时位置：data[0]=符号, data[1..4]=原始实时位置 */
      rawVal = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) |
               ((uint32_t)data[3] <<  8) |  (uint32_t)data[4];
#if defined(STEPPER_FIRMWARE_X)
      /* X固件：原始值 / 10 = 累计角度，3600 个原始计数表示一圈。 */
      realPosRevs      = (float)rawVal / 3600.0f;
      realPosAngle_deg = (float)(rawVal % 3600U) / 10.0f;
#else
      /* Emm固件：65536 个原始计数表示一圈。 */
      realPosRevs      = (float)rawVal / 65536.0f;
      realPosAngle_deg = ((float)(rawVal % 65536U) * 360.0f) / 65536.0f;
#endif
      if(data[0] == 0x00U) {
        info->realPosRevs = realPosRevs;
        info->realPosAngle_deg = realPosAngle_deg;
      } else {
        info->realPosRevs = -realPosRevs;
        info->realPosAngle_deg = (realPosAngle_deg == 0.0f) ?
                                  0.0f : (360.0f - realPosAngle_deg);
      }
      break;
    case 0x35U:  /* 读取实时转速：data[0]=符号, data[1..2]=转速(0.1RPM) */
      rawVal = ((uint32_t)data[1] << 8) | (uint32_t)data[2];
      info->realSpd_01rpm = (int16_t)((data[0] == 0x00U) ? (int32_t)rawVal : -(int32_t)rawVal);
      break;
    case 0x31U:  /* 读取线性化编码器值：data[0..1]=编码器值 */
      info->encoder = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
      break;
    case 0x37U:  /* 读取位置误差：data[0]=符号, data[1..4]=误差(0.01°) */
      rawVal = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) |
               ((uint32_t)data[3] <<  8) |  (uint32_t)data[4];
      info->posErr_001deg = (data[0] == 0x00U) ? (int32_t)rawVal : -(int32_t)rawVal;
      break;
    case 0x26U:  /* 读取总线电流：data[0..1]=CBus */
      info->busCurrent = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
      break;
    case 0x27U:  /* 读取相电流：data[0..1]=相电流(mA) */
      rawVal = ((uint32_t)data[0] << 8) | (uint32_t)data[1];
      info->phaseCurrent_A = (float)rawVal / 1000.0f;
      break;
    case 0x39U:  /* 读取驱动温度：data[0]=符号, data[1]=温度 */
      info->driverTemp = (data[0] == 0x00U) ? (int16_t)data[1] : -(int16_t)data[1];
      break;
    case 0x3AU:  /* 读取电机状态标志：data[0]=标志字节 */
      info->motorStatus = data[0];
      break;
    case 0x3BU:  /* 读取回零状态标志：data[0]=标志字节 */
      info->homeStatus = data[0];
      break;
    case 0x3CU:  /* 读取回零+电机标志：data[0]=回零标志, data[1]=电机标志 */
      info->homeStatus  = data[0];
      info->motorStatus = data[1];
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

__weak void DRIVER_STEPPER_Axis0CtrlHook(void) {
  //0x01 电机控制函数
}

__weak void DRIVER_STEPPER_Axis1CtrlHook(void) {
  //0x02 电机控制函数
}

/*============================================================================
 * API接口 — 两套固件通用
 *============================================================================*/

/* 初始化步进电机驱动，配置两轴地址并注册 USART2 接收回调。 */
void DRIVER_STEPPER_Init(void) {
  memset(stepperMotorInfo, 0, sizeof(stepperMotorInfo));
  memset(&sParseCtx, 0, sizeof(sParseCtx));
  sUpdateStep = 0U;
  sStatusUpdateCnt  = 0U;
  sStatusUpdateStep = 0U;
  sStatusAxisIdx    = 0U;

  stepperMotorInfo[0].addr = STEPPER_ADDR_PITCH;
  stepperMotorInfo[1].addr = STEPPER_ADDR_YAW;
  sParseCtx.state = STEPPER_PARSE_WAIT_ADDR;

  STEPPER_DEP_UART_SET_CB(__STEPPER_RxByteCallback);

  DRIVER_STEPPER_Enable(STEPPER_ADDR_PITCH, 1);
  DRIVER_STEPPER_Enable(STEPPER_ADDR_YAW,   1);
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

/* 发送读取电机状态标志请求（功能码 3A）。 */
void DRIVER_STEPPER_ReadStatus(uint8_t motorId) {
  uint8_t len = 0U;
  sTxBuf[len++] = motorId;
  sTxBuf[len++] = 0x3AU;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取回零状态标志请求（功能码 3B）。 */
void DRIVER_STEPPER_ReadHomeStatus(uint8_t motorId) {
  uint8_t len = 0U;
  sTxBuf[len++] = motorId;
  sTxBuf[len++] = 0x3BU;
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

/* 发送读取总线电流请求（功能码 26）。 */
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

/* 参数查询/控制发送状态机（2ms 任务周期调用）。
 * 0: 轴0转速  1: 轴0位置  2: 轴1转速  3: 轴1位置
 * 4: 轴0控制弱钩子  5: 轴1控制弱钩子
 */
void DRIVER_STEPPER_Update(void) {
  sStatusUpdateCnt++;
  if(sStatusUpdateCnt >= STEPPER_STATUS_UPDATE_DIV) {
    sStatusUpdateCnt = 0U;

    switch(sStatusUpdateStep) {
      case 0U:
        DRIVER_STEPPER_ReadBusCurrent(stepperMotorInfo[sStatusAxisIdx].addr);
        break;
      case 1U:
        DRIVER_STEPPER_ReadPhaseCurrent(stepperMotorInfo[sStatusAxisIdx].addr);
        break;
      case 2U:
        DRIVER_STEPPER_ReadDriverTemp(stepperMotorInfo[sStatusAxisIdx].addr);
        sStatusAxisIdx++;
        if(sStatusAxisIdx >= STEPPER_MOTOR_CNT) {
          sStatusAxisIdx = 0U;
        }
        break;
      default:
        sStatusUpdateStep = 0U;
        DRIVER_STEPPER_ReadBusCurrent(stepperMotorInfo[sStatusAxisIdx].addr);
        break;
    }

    sStatusUpdateStep++;
    if(sStatusUpdateStep >= STEPPER_STATUS_UPDATE_STEPS) {
      sStatusUpdateStep = 0U;
    }
    return;
  }

  switch(sUpdateStep) {
    case 0U:
      DRIVER_STEPPER_ReadSpeed(stepperMotorInfo[0].addr);
      break;
    case 1U:
      DRIVER_STEPPER_ReadPos(stepperMotorInfo[0].addr);
      break;
    case 2U:
      DRIVER_STEPPER_ReadSpeed(stepperMotorInfo[1].addr);
      break;
    case 3U:
      DRIVER_STEPPER_ReadPos(stepperMotorInfo[1].addr);
      break;
    case 4U:
      //0x01 电机控制函数
      __STEPPER_RunCtrlHook(DRIVER_STEPPER_Axis0CtrlHook);
      break;
    case 5U:
      //0x02 电机控制函数
      __STEPPER_RunCtrlHook(DRIVER_STEPPER_Axis1CtrlHook);
      break;
    default:
      sUpdateStep = 0U;
      DRIVER_STEPPER_ReadSpeed(stepperMotorInfo[0].addr);
      break;
  }

  sUpdateStep++;
  if(sUpdateStep >= 6U) {
    sUpdateStep = 0U;
  }
}

/*============================================================================
 * API接口 — 固件专有
 *============================================================================*/

/* 转动指定圈数（相对当前位置）。 */
void DRIVER_STEPPER_RotateRevs(uint8_t motorId, int16_t speed, uint8_t acc, uint32_t revs) {
  uint8_t  dir      = (speed < 0) ? 0x01U : 0x00U;
  uint16_t absSpeed = (speed < 0) ? (uint16_t)(-speed) : (uint16_t)speed;
  uint8_t  txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t  len      = 0U;

#if defined(STEPPER_FIRMWARE_X)
  /* X固件：使用梯形曲线位置模式（功能码 FD），位置单位 0.1°，speed 单位 0.1RPM，acc 单位 RPM/S。 */
  uint32_t pos      = revs * 3600U;
  uint16_t spd      = (uint16_t)(absSpeed * 10U);  /* RPM → 0.1RPM */
  uint16_t accel    = (uint16_t)acc;
  txBuf[len++] = motorId;
  txBuf[len++] = 0xFDU;
  txBuf[len++] = dir;
  txBuf[len++] = (uint8_t)(accel >> 8);
  txBuf[len++] = (uint8_t)(accel & 0xFFU);
  txBuf[len++] = (uint8_t)(accel >> 8);
  txBuf[len++] = (uint8_t)(accel & 0xFFU);
  txBuf[len++] = (uint8_t)(spd >> 8);
  txBuf[len++] = (uint8_t)(spd & 0xFFU);
  txBuf[len++] = (uint8_t)(pos >> 24);
  txBuf[len++] = (uint8_t)(pos >> 16);
  txBuf[len++] = (uint8_t)(pos >>  8);
  txBuf[len++] = (uint8_t)(pos & 0xFFU);
  txBuf[len++] = STEPPER_MODE_REL_CUR;
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */
#else
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
#endif

  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

#if defined(STEPPER_FIRMWARE_X)

/* 力矩模式控制（功能码 F5，X固件）。 */
void DRIVER_STEPPER_SetTorque(uint8_t motorId, uint16_t slope, int16_t current) {
  uint8_t  dir     = (current < 0) ? 0x01U : 0x00U;
  uint16_t absCur  = (current < 0) ? (uint16_t)(-current) : (uint16_t)current;
  uint8_t  txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t  len     = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0xF5U;
  txBuf[len++] = dir;
  txBuf[len++] = (uint8_t)(slope >> 8);
  txBuf[len++] = (uint8_t)(slope & 0xFFU);
  txBuf[len++] = (uint8_t)(absCur >> 8);
  txBuf[len++] = (uint8_t)(absCur & 0xFFU);
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

/* 速度模式控制（功能码 F6，X固件）。 */
void DRIVER_STEPPER_SetSpeed(uint8_t motorId, uint16_t accel, int16_t speed) {
  uint8_t  dir      = (speed < 0) ? 0x01U : 0x00U;
  uint16_t absSpeed = (speed < 0) ? (uint16_t)(-speed) : (uint16_t)speed;
  uint8_t  txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t  len      = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0xF6U;
  txBuf[len++] = dir;
  txBuf[len++] = (uint8_t)(accel >> 8);
  txBuf[len++] = (uint8_t)(accel & 0xFFU);
  txBuf[len++] = (uint8_t)(absSpeed >> 8);
  txBuf[len++] = (uint8_t)(absSpeed & 0xFFU);
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

/* 直通限速位置模式控制（功能码 FB，X固件）。 */
void DRIVER_STEPPER_SetPosDirect(uint8_t motorId, int16_t speed,
                                 uint32_t pos, uint8_t mode) {
  uint8_t  dir      = (speed < 0) ? 0x01U : 0x00U;
  uint16_t absSpeed = (speed < 0) ? (uint16_t)(-speed) : (uint16_t)speed;
  uint8_t  txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t  len      = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0xFBU;
  txBuf[len++] = dir;
  txBuf[len++] = (uint8_t)(absSpeed >> 8);
  txBuf[len++] = (uint8_t)(absSpeed & 0xFFU);
  txBuf[len++] = (uint8_t)(pos >> 24);
  txBuf[len++] = (uint8_t)(pos >> 16);
  txBuf[len++] = (uint8_t)(pos >>  8);
  txBuf[len++] = (uint8_t)(pos & 0xFFU);
  txBuf[len++] = mode;
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

/* 梯形曲线加减速位置模式控制（功能码 FD，X固件）。 */
void DRIVER_STEPPER_SetPosTrapezoid(uint8_t motorId, uint16_t accAccel,
                                    uint16_t decAccel, int16_t maxSpeed,
                                    uint32_t pos, uint8_t mode) {
  uint8_t  dir      = (maxSpeed < 0) ? 0x01U : 0x00U;
  uint16_t absSpeed = (maxSpeed < 0) ? (uint16_t)(-maxSpeed) : (uint16_t)maxSpeed;
  uint8_t  txBuf[STEPPER_TX_BUF_SIZE];
  uint8_t  len      = 0U;
  txBuf[len++] = motorId;
  txBuf[len++] = 0xFDU;
  txBuf[len++] = dir;
  txBuf[len++] = (uint8_t)(accAccel >> 8);
  txBuf[len++] = (uint8_t)(accAccel & 0xFFU);
  txBuf[len++] = (uint8_t)(decAccel >> 8);
  txBuf[len++] = (uint8_t)(decAccel & 0xFFU);
  txBuf[len++] = (uint8_t)(absSpeed >> 8);
  txBuf[len++] = (uint8_t)(absSpeed & 0xFFU);
  txBuf[len++] = (uint8_t)(pos >> 24);
  txBuf[len++] = (uint8_t)(pos >> 16);
  txBuf[len++] = (uint8_t)(pos >>  8);
  txBuf[len++] = (uint8_t)(pos & 0xFFU);
  txBuf[len++] = mode;
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

#else  /* STEPPER_FIRMWARE_EMM */

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
void DRIVER_STEPPER_SetPos(uint8_t motorId, int16_t vel, uint8_t acc, uint32_t clk) {
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
  txBuf[len++] = STEPPER_MODE_REL_CUR;
  txBuf[len++] = 0x00U;  /* sync: 立即执行 */
  txBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(txBuf, len);
}

#endif  /* STEPPER_FIRMWARE_X */
