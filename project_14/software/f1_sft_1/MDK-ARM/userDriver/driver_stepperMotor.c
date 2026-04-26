/*============================================================================
 * README
 * 步进电机驱动模块（ZDT X42S 第二代闭环步进电机，X固件）
 * 通讯接口：USART2，波特率 115200，TTL 电平
 * 帧格式：[Addr][FuncCode][Data...][0x6B]，校验码固定为 0x6B
 * 本模块管理两轴（俯仰/偏航），地址分别为 0x01 / 0x02
 *============================================================================*/

#include "driver_stepperMotor.h"
#include <string.h>

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* 接收缓冲区大小（功能码后最多 6 字节数据 + 1 字节校验）。 */
#define STEPPER_RX_BUF_SIZE   8U

/* 发送帧最大长度（梯形位置模式最长 16 字节）。 */
#define STEPPER_TX_BUF_SIZE   20U

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
  uint8_t  axisIdx;                      /* 当前正在解析的轴索引 */
  uint8_t  funcCode;                     /* 当前功能码 */
  uint8_t  rxBuf[STEPPER_RX_BUF_SIZE];  /* 功能码后的数据缓冲 */
  uint8_t  rxCnt;                        /* 已收到的字节数 */
  uint8_t  expectedCnt;                  /* 功能码后需接收的字节总数（含校验）*/
} stepperParseCtx_t;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 步进电机模块信息（公有，供上层直接访问）。 */
stepperMotorInfo_t stepperMotorInfo[STEPPER_MOTOR_CNT];

/* 接收解析上下文。 */
static stepperParseCtx_t sParseCtx;

/* 发送缓冲区（仅在任务上下文中使用，不存在并发写入）。 */
static uint8_t sTxBuf[STEPPER_TX_BUF_SIZE];

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
    /* 线性化编码器 / 回零+电机标志：2字节值+0x6B = 3 */
    case 0x31U: case 0x3CU: return 3U;
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

  switch(funcCode) {
    case 0x36U:  /* 读取实时位置：data[0]=符号, data[1..4]=角度(0.1°) */
      rawVal = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) |
               ((uint32_t)data[3] <<  8) |  (uint32_t)data[4];
      info->realPos_01deg = (data[0] == 0x00U) ? (int32_t)rawVal : -(int32_t)rawVal;
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
  (void)port;

  switch(sParseCtx.state) {
    case STEPPER_PARSE_WAIT_ADDR:
      if(byte == stepperMotorInfo[STEPPER_AXIS_PITCH].addr) {
        sParseCtx.axisIdx = STEPPER_AXIS_PITCH;
        sParseCtx.state   = STEPPER_PARSE_WAIT_FUNC;
      } else if(byte == stepperMotorInfo[STEPPER_AXIS_YAW].addr) {
        sParseCtx.axisIdx = STEPPER_AXIS_YAW;
        sParseCtx.state   = STEPPER_PARSE_WAIT_FUNC;
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

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化步进电机驱动，配置两轴地址并注册 USART2 接收回调。 */
void DRIVER_STEPPER_Init(void) {
  memset(stepperMotorInfo, 0, sizeof(stepperMotorInfo));
  memset(&sParseCtx, 0, sizeof(sParseCtx));

  stepperMotorInfo[STEPPER_AXIS_PITCH].addr = STEPPER_ADDR_PITCH;
  stepperMotorInfo[STEPPER_AXIS_YAW].addr   = STEPPER_ADDR_YAW;
  sParseCtx.state = STEPPER_PARSE_WAIT_ADDR;

  STEPPER_DEP_UART_SET_CB(__STEPPER_RxByteCallback);
}

/* 使能/失能电机（功能码 F3）。 */
void DRIVER_STEPPER_Enable(uint8_t axis, uint8_t enable, uint8_t sync) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0xF3U;
  sTxBuf[len++] = 0xABU;
  sTxBuf[len++] = (enable != 0U) ? 0x01U : 0x00U;
  sTxBuf[len++] = sync;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 力矩模式控制（功能码 F5，X固件）。 */
void DRIVER_STEPPER_SetTorque(uint8_t axis, uint8_t dir, uint16_t slope,
                              uint16_t current, uint8_t sync) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0xF5U;
  sTxBuf[len++] = (dir != 0U) ? 0x01U : 0x00U;
  sTxBuf[len++] = (uint8_t)(slope >> 8);
  sTxBuf[len++] = (uint8_t)(slope & 0xFFU);
  sTxBuf[len++] = (uint8_t)(current >> 8);
  sTxBuf[len++] = (uint8_t)(current & 0xFFU);
  sTxBuf[len++] = sync;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 速度模式控制（功能码 F6，X固件）。 */
void DRIVER_STEPPER_SetSpeed(uint8_t axis, uint8_t dir, uint16_t accel,
                             uint16_t speed, uint8_t sync) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0xF6U;
  sTxBuf[len++] = (dir != 0U) ? 0x01U : 0x00U;
  sTxBuf[len++] = (uint8_t)(accel >> 8);
  sTxBuf[len++] = (uint8_t)(accel & 0xFFU);
  sTxBuf[len++] = (uint8_t)(speed >> 8);
  sTxBuf[len++] = (uint8_t)(speed & 0xFFU);
  sTxBuf[len++] = sync;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 直通限速位置模式控制（功能码 FB，X固件）。 */
void DRIVER_STEPPER_SetPosDirect(uint8_t axis, uint8_t dir, uint16_t speed,
                                 uint32_t pos, uint8_t mode, uint8_t sync) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0xFBU;
  sTxBuf[len++] = (dir != 0U) ? 0x01U : 0x00U;
  sTxBuf[len++] = (uint8_t)(speed >> 8);
  sTxBuf[len++] = (uint8_t)(speed & 0xFFU);
  sTxBuf[len++] = (uint8_t)(pos >> 24);
  sTxBuf[len++] = (uint8_t)(pos >> 16);
  sTxBuf[len++] = (uint8_t)(pos >>  8);
  sTxBuf[len++] = (uint8_t)(pos & 0xFFU);
  sTxBuf[len++] = mode;
  sTxBuf[len++] = sync;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 梯形曲线加减速位置模式控制（功能码 FD，X固件）。 */
void DRIVER_STEPPER_SetPosTrapezoid(uint8_t axis, uint8_t dir,
                                    uint16_t accAccel, uint16_t decAccel,
                                    uint16_t maxSpeed, uint32_t pos,
                                    uint8_t mode, uint8_t sync) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0xFDU;
  sTxBuf[len++] = (dir != 0U) ? 0x01U : 0x00U;
  sTxBuf[len++] = (uint8_t)(accAccel >> 8);
  sTxBuf[len++] = (uint8_t)(accAccel & 0xFFU);
  sTxBuf[len++] = (uint8_t)(decAccel >> 8);
  sTxBuf[len++] = (uint8_t)(decAccel & 0xFFU);
  sTxBuf[len++] = (uint8_t)(maxSpeed >> 8);
  sTxBuf[len++] = (uint8_t)(maxSpeed & 0xFFU);
  sTxBuf[len++] = (uint8_t)(pos >> 24);
  sTxBuf[len++] = (uint8_t)(pos >> 16);
  sTxBuf[len++] = (uint8_t)(pos >>  8);
  sTxBuf[len++] = (uint8_t)(pos & 0xFFU);
  sTxBuf[len++] = mode;
  sTxBuf[len++] = sync;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 立即停止电机（功能码 FE）。 */
void DRIVER_STEPPER_Stop(uint8_t axis, uint8_t sync) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0xFEU;
  sTxBuf[len++] = 0x98U;
  sTxBuf[len++] = sync;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 触发回零（功能码 9A）。 */
void DRIVER_STEPPER_GoHome(uint8_t axis, uint8_t homeMode, uint8_t sync) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0x9AU;
  sTxBuf[len++] = homeMode;
  sTxBuf[len++] = sync;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 将当前位置角度清零（功能码 0A）。 */
void DRIVER_STEPPER_ZeroPos(uint8_t axis) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0x0AU;
  sTxBuf[len++] = 0x6DU;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 解除堵转/过热/过流保护（功能码 0E）。 */
void DRIVER_STEPPER_ClearError(uint8_t axis) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0x0EU;
  sTxBuf[len++] = 0x52U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取实时位置请求（功能码 36）。 */
void DRIVER_STEPPER_ReadPos(uint8_t axis) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0x36U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取实时转速请求（功能码 35）。 */
void DRIVER_STEPPER_ReadSpeed(uint8_t axis) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0x35U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取线性化编码器值请求（功能码 31）。 */
void DRIVER_STEPPER_ReadEncoder(uint8_t axis) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0x31U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取电机状态标志请求（功能码 3A）。 */
void DRIVER_STEPPER_ReadStatus(uint8_t axis) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0x3AU;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取回零状态标志请求（功能码 3B）。 */
void DRIVER_STEPPER_ReadHomeStatus(uint8_t axis) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0x3BU;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}

/* 发送读取位置误差请求（功能码 37）。 */
void DRIVER_STEPPER_ReadPosError(uint8_t axis) {
  uint8_t len = 0U;
  sTxBuf[len++] = stepperMotorInfo[axis].addr;
  sTxBuf[len++] = 0x37U;
  sTxBuf[len++] = STEPPER_CHECKSUM;
  STEPPER_DEP_UART_SEND_RAW(sTxBuf, len);
}
