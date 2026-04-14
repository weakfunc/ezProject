/*============================================================================
 * README
 * ATK-MS901M 十轴高性能角度传感器模块驱动
 * 通过 USART3 以自定义协议接收模块主动上报数据帧，实时解析并写入 imu901Info。
 * 帧格式：[0x55][0x55][ID][LEN][DATA×LEN][SUM]
 * SUM = 0x55+0x55+ID+LEN+DATA[0]+...+DATA[LEN-1]（不计溢出）
 *============================================================================*/

#include "driver_imu901.h"
#include "stdlib_common.h"
#include <string.h>

/*============================================================================
 * 内部配置
 *============================================================================*/

/* 加速度计默认满量程（G），对应寄存器默认值 0x01 = 4G */
#define IMU901_ACC_FSR          4.0f

/* 陀螺仪默认满量程（°/s），对应寄存器默认值 0x03 = 2000dps */
#define IMU901_GYRO_FSR         2000.0f

/* 帧数据段最大长度（字节），帧0x03数据段最长为12字节 */
#define IMU901_DATA_BUF_SIZE    16U

/* 帧ID定义 */
#define IMU901_ID_EULER         0x01U  /* 姿态角帧 */
#define IMU901_ID_QUAT          0x02U  /* 四元数帧 */
#define IMU901_ID_GYRACC        0x03U  /* 陀螺仪+加速度计帧 */
#define IMU901_ID_MAG           0x04U  /* 磁力计帧 */
#define IMU901_ID_BARO          0x05U  /* 气压计帧 */
#define IMU901_ID_PORT          0x06U  /* 端口状态帧 */

/* 协议解析状态机状态 */
typedef enum {
  IMU901_WAIT_SOF1 = 0,  /* 等待帧头1（0x55） */
  IMU901_WAIT_SOF2,       /* 等待帧头2（0x55） */
  IMU901_WAIT_ID,         /* 接收帧ID */
  IMU901_WAIT_LEN,        /* 接收数据长度 */
  IMU901_RECV_DATA,       /* 接收数据段 */
  IMU901_WAIT_SUM,        /* 接收校验和并验证 */
} imu901ParseState_e;

/* 帧解析器私有上下文 */
typedef struct {
  imu901ParseState_e state;                     /* 当前解析状态 */
  uint8_t            id;                         /* 当前帧ID */
  uint8_t            len;                        /* 当前帧数据段长度（字节） */
  uint8_t            data[IMU901_DATA_BUF_SIZE]; /* 数据段临时缓冲 */
  uint8_t            dataIdx;                    /* 已接收数据字节数 */
  uint8_t            sum;                        /* 累积校验和（含帧头至数据段） */
  uint32_t           frameOkCnt;                /* 校验通过帧计数 */
  uint32_t           sumErrCnt;                 /* 校验失败帧计数 */
} imu901Parser_t;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* IMU901模块公共数据（对外暴露） */
imu901Info_t imu901Info;

/* 解析器上下文（私有） */
static imu901Parser_t imu901Parser;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 重置解析状态机至初始等待帧头状态 */
static void __DRIVER_IMU901_ResetParser(void){
  imu901Parser.state   = IMU901_WAIT_SOF1;
  imu901Parser.dataIdx = 0U;
  imu901Parser.sum     = 0U;
}

/* 解析姿态角帧（ID=0x01，LEN=6）
 * 数据格式：RollL RollH PitchL PitchH YawL YawH
 */
static void __DRIVER_IMU901_ParseEuler(const uint8_t *d){
  int16_t raw;
  raw = (int16_t)(((uint16_t)d[1] << 8U) | (uint16_t)d[0]);
  imu901Info.roll  = (float)raw / 32768.0f * 180.0f;
  raw = (int16_t)(((uint16_t)d[3] << 8U) | (uint16_t)d[2]);
  imu901Info.pitch = (float)raw / 32768.0f * 180.0f;
  raw = (int16_t)(((uint16_t)d[5] << 8U) | (uint16_t)d[4]);
  imu901Info.yaw   = (float)raw / 32768.0f * 180.0f;
}

/* 解析四元数帧（ID=0x02，LEN=8）
 * 数据格式：Q0L Q0H Q1L Q1H Q2L Q2H Q3L Q3H
 */
static void __DRIVER_IMU901_ParseQuat(const uint8_t *d){
  int16_t raw;
  raw = (int16_t)(((uint16_t)d[1] << 8U) | (uint16_t)d[0]);
  imu901Info.q0 = (float)raw / 32768.0f;
  raw = (int16_t)(((uint16_t)d[3] << 8U) | (uint16_t)d[2]);
  imu901Info.q1 = (float)raw / 32768.0f;
  raw = (int16_t)(((uint16_t)d[5] << 8U) | (uint16_t)d[4]);
  imu901Info.q2 = (float)raw / 32768.0f;
  raw = (int16_t)(((uint16_t)d[7] << 8U) | (uint16_t)d[6]);
  imu901Info.q3 = (float)raw / 32768.0f;
}

/* 解析陀螺仪+加速度计帧（ID=0x03，LEN=12）
 * 数据格式：AxL AxH AyL AyH AzL AzH GxL GxH GyL GyH GzL GzH
 */
static void __DRIVER_IMU901_ParseGyrAcc(const uint8_t *d){
  int16_t raw;
  /* 加速度计（单位：G，满量程 IMU901_ACC_FSR） */
  raw = (int16_t)(((uint16_t)d[1] << 8U) | (uint16_t)d[0]);
  imu901Info.accX = (float)raw / 32768.0f * IMU901_ACC_FSR;
  raw = (int16_t)(((uint16_t)d[3] << 8U) | (uint16_t)d[2]);
  imu901Info.accY = (float)raw / 32768.0f * IMU901_ACC_FSR;
  raw = (int16_t)(((uint16_t)d[5] << 8U) | (uint16_t)d[4]);
  imu901Info.accZ = (float)raw / 32768.0f * IMU901_ACC_FSR;
  /* 陀螺仪（单位：°/s，满量程 IMU901_GYRO_FSR） */
  raw = (int16_t)(((uint16_t)d[7] << 8U) | (uint16_t)d[6]);
  imu901Info.gyroX = (float)raw / 32768.0f * IMU901_GYRO_FSR;
  raw = (int16_t)(((uint16_t)d[9] << 8U) | (uint16_t)d[8]);
  imu901Info.gyroY = (float)raw / 32768.0f * IMU901_GYRO_FSR;
  raw = (int16_t)(((uint16_t)d[11] << 8U) | (uint16_t)d[10]);
  imu901Info.gyroZ = (float)raw / 32768.0f * IMU901_GYRO_FSR;
}

/* 解析磁力计帧（ID=0x04，LEN=8）
 * 数据格式：MxL MxH MyL MyH MzL MzH TL TH
 */
static void __DRIVER_IMU901_ParseMag(const uint8_t *d){
  int16_t rawT;
  imu901Info.magX = (int16_t)(((uint16_t)d[1] << 8U) | (uint16_t)d[0]);
  imu901Info.magY = (int16_t)(((uint16_t)d[3] << 8U) | (uint16_t)d[2]);
  imu901Info.magZ = (int16_t)(((uint16_t)d[5] << 8U) | (uint16_t)d[4]);
  rawT = (int16_t)(((uint16_t)d[7] << 8U) | (uint16_t)d[6]);
  imu901Info.magTemp = (float)rawT / 100.0f;
}

/* 解析气压计帧（ID=0x05，LEN=10）
 * 数据格式：P0 P1 P2 P3 A0 A1 A2 A3 TL TH
 */
static void __DRIVER_IMU901_ParseBaro(const uint8_t *d){
  int16_t rawT;
  imu901Info.pressure = (int32_t)(((uint32_t)d[3] << 24U) | ((uint32_t)d[2] << 16U) |
                                   ((uint32_t)d[1] << 8U)  |  (uint32_t)d[0]);
  imu901Info.altitude = (int32_t)(((uint32_t)d[7] << 24U) | ((uint32_t)d[6] << 16U) |
                                   ((uint32_t)d[5] << 8U)  |  (uint32_t)d[4]);
  rawT = (int16_t)(((uint16_t)d[9] << 8U) | (uint16_t)d[8]);
  imu901Info.baroTemp = (float)rawT / 100.0f;
}

/* 解析端口状态帧（ID=0x06，LEN=8）
 * 数据格式：D0L D0H D1L D1H D2L D2H D3L D3H
 */
static void __DRIVER_IMU901_ParsePort(const uint8_t *d){
  imu901Info.portD[0] = (uint16_t)(((uint16_t)d[1] << 8U) | (uint16_t)d[0]);
  imu901Info.portD[1] = (uint16_t)(((uint16_t)d[3] << 8U) | (uint16_t)d[2]);
  imu901Info.portD[2] = (uint16_t)(((uint16_t)d[5] << 8U) | (uint16_t)d[4]);
  imu901Info.portD[3] = (uint16_t)(((uint16_t)d[7] << 8U) | (uint16_t)d[6]);
}

/* 校验通过后，按帧ID分发至对应解析函数 */
static void __DRIVER_IMU901_ProcessFrame(void){
  VAR_ADD(imu901Parser.frameOkCnt)
  switch(imu901Parser.id){
  case IMU901_ID_EULER:  __DRIVER_IMU901_ParseEuler(imu901Parser.data);  break;
  case IMU901_ID_QUAT:   __DRIVER_IMU901_ParseQuat(imu901Parser.data);   break;
  case IMU901_ID_GYRACC: __DRIVER_IMU901_ParseGyrAcc(imu901Parser.data); break;
  case IMU901_ID_MAG:    __DRIVER_IMU901_ParseMag(imu901Parser.data);    break;
  case IMU901_ID_BARO:   __DRIVER_IMU901_ParseBaro(imu901Parser.data);   break;
  case IMU901_ID_PORT:   __DRIVER_IMU901_ParsePort(imu901Parser.data);   break;
  default: break;
  }
}

/* 串口3自定义字节回调：逐字节驱动ATK-MS901M帧解析状态机。
 * port：串口端口号（由stdlib_usart传入，始终为UART_PORT3，此处不使用）
 * byte：本次接收到的字节
 */
static void __DRIVER_IMU901_RxCallback(uint8_t port, uint8_t byte){
  (void)port;  /* 端口已由初始化绑定，此处无需使用 */

  switch(imu901Parser.state){
  case IMU901_WAIT_SOF1:
    if(byte == 0x55U){
      imu901Parser.sum   = 0x55U;
      imu901Parser.state = IMU901_WAIT_SOF2;
    }
    break;

  case IMU901_WAIT_SOF2:
    if(byte == 0x55U){
      imu901Parser.sum  += 0x55U;
      imu901Parser.state = IMU901_WAIT_ID;
    } else {
      /* 帧头2错误，重置解析器 */
      __DRIVER_IMU901_ResetParser();
    }
    break;

  case IMU901_WAIT_ID:
    imu901Parser.id    = byte;
    imu901Parser.sum  += byte;
    imu901Parser.state = IMU901_WAIT_LEN;
    break;

  case IMU901_WAIT_LEN:
    if((byte == 0U) || (byte > IMU901_DATA_BUF_SIZE)){
      /* 数据长度非法，重置解析器 */
      __DRIVER_IMU901_ResetParser();
      break;
    }
    imu901Parser.len     = byte;
    imu901Parser.sum    += byte;
    imu901Parser.dataIdx = 0U;
    imu901Parser.state   = IMU901_RECV_DATA;
    break;

  case IMU901_RECV_DATA:
    imu901Parser.data[imu901Parser.dataIdx++] = byte;
    imu901Parser.sum += byte;
    if(imu901Parser.dataIdx >= imu901Parser.len){
      imu901Parser.state = IMU901_WAIT_SUM;
    }
    break;

  case IMU901_WAIT_SUM:
    if(byte == imu901Parser.sum){
      __DRIVER_IMU901_ProcessFrame();
    } else {
      VAR_ADD(imu901Parser.sumErrCnt)
    }
    __DRIVER_IMU901_ResetParser();
    break;

  default:
    __DRIVER_IMU901_ResetParser();
    break;
  }
}

/*============================================================================
 * API接口实现
 *============================================================================*/

/* 初始化IMU901驱动：清零数据，向stdlib_usart注册USART3自定义字节回调 */
void DRIVER_IMU901_Init(void){
  memset(&imu901Info,   0, sizeof(imu901Info));
  memset(&imu901Parser, 0, sizeof(imu901Parser));
  imu901Parser.state = IMU901_WAIT_SOF1;
  STDLIB_USART_SetCustomCb(IMU901_DEP_UART_PORT, __DRIVER_IMU901_RxCallback);
}

/* 更新IMU901数据（task层10ms周期调用）。
 * 数据已在USART3中断回调中实时解析并写入imu901Info，此处预留供后续扩展。
 */
void DRIVER_IMU901_Update(void){
}
