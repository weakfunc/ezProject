#include "driver_senser.h"
#include <string.h>

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* Modbus RTU 请求帧长度（字节）。 */
#define ULT_REQ_LEN       (8U)
/* Modbus RTU 应答帧长度（字节）。 */
#define ULT_RSP_LEN       (7U)
/* 应答帧功能码字节索引。 */
#define ULT_RSP_FC_IDX    (1U)
/* 应答帧字节数字节索引。 */
#define ULT_RSP_BC_IDX    (2U)
/* 应答帧距离高字节索引。 */
#define ULT_RSP_DH_IDX    (3U)
/* 应答帧距离低字节索引。 */
#define ULT_RSP_DL_IDX    (4U)
/* 应答帧 CRC 低字节索引。 */
#define ULT_RSP_CRCL_IDX  (5U)
/* 应答帧 CRC 高字节索引。 */
#define ULT_RSP_CRCH_IDX  (6U)

/*============================================================================
 * 私有类型定义
 *============================================================================*/

/* 应答帧接收状态（由字节回调填充，主任务轮询）。 */
typedef struct {
  uint8_t buf[ULT_RSP_LEN]; /* 应答帧字节缓存 */
  uint8_t cnt;              /* 已接收字节数 */
  uint8_t ready;            /* 帧接收完成标志（1：完成；0：未完成） */
} ultRspState_t;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 超声波传感器信息（公有，供上层直接访问）。 */
senserInfo_t senserInfo;

/* 应答帧接收状态（私有）。 */
static ultRspState_t ultRspState;

/* Modbus RTU 读距离请求帧（设备地址 0x01，功能码 0x03，寄存器 0x0000，数量 1）。
 * CRC16：0x840A（低字节 0x84 在前）。
 */
static uint8_t ultReqFrame[ULT_REQ_LEN] = {
  0x01U, 0x03U, 0x00U, 0x00U, 0x00U, 0x01U, 0x84U, 0x0AU
};

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 计算 Modbus RTU CRC16（多项式 0xA001，初始值 0xFFFF）。
 * data : 待校验字节数组首地址；
 * len  : 字节数。
 */
static uint16_t __DRIVER_SENSER_Crc16Calc(const uint8_t *data, uint8_t len){
  uint16_t crc = 0xFFFFU;
  uint8_t i, j;
  for(i = 0U; i < len; i++){
    crc ^= (uint16_t)data[i];
    for(j = 0U; j < 8U; j++){
      if(crc & 0x0001U){
        crc = (crc >> 1U) ^ 0xA001U;
      } else {
        crc >>= 1U;
      }
    }
  }
  return crc;
}

/* 自定义字节回调：由 stdlib_usart 在中断中逐字节触发，收集 Modbus RTU 应答帧。
 * 收满 ULT_RSP_LEN 字节后置位 ready 标志，多余字节自动丢弃。
 */
static void __DRIVER_SENSER_UltParseCb(uint8_t port, uint8_t byte){
  (void)port;
  if(ultRspState.cnt < ULT_RSP_LEN){
    ultRspState.buf[ultRspState.cnt] = byte;
    ultRspState.cnt++;
    if(ultRspState.cnt >= ULT_RSP_LEN){
      ultRspState.ready = 1U;
    }
  }
}

/*============================================================================
 * API接口
 *============================================================================*/

/* AHT10 初始化命令数据字节（0xE1 命令后跟 0x08 0x00）。 */
static const uint8_t aht10InitData[2] = {0x08U, 0x00U};
/* AHT10 触发采集命令数据字节（0xAC 命令后跟 0x33 0x00）。 */
static const uint8_t aht10TrigData[2] = {0x33U, 0x00U};

/* 初始化传感器驱动：
 * 1. 注册 UART_PORT2 字节回调以接收 Modbus RTU 应答帧（波特率由 CubeMX 配置为 9600）；
 * 2. 将 TRIG 引脚置低，准备 HC-SR04 触发；
 * 3. 向 AHT10 发送初始化命令并等待校准完成。
 */
void DRIVER_SENSER_Init(void){
  memset(&senserInfo, 0, sizeof(senserInfo));
  memset(&ultRspState, 0, sizeof(ultRspState));
  SENSER_ULT_DEP_UART_SET_CB(__DRIVER_SENSER_UltParseCb);
  SENSER_HCSR04_DEP_TRIG_SET(GPIO_LEVEL_LOW);
  /* AHT10 初始化：发送 0xE1 命令，等待 75ms 完成校准 */
  SENSER_AHT10_DEP_WRITE(0xE1U, aht10InitData, 2U);
  SENSER_AHT10_DEP_DELAY_MS(SENSER_AHT10_INIT_WAIT_MS);
}

/* 发送 Modbus RTU 读距离请求，阻塞等待应答（最长 SENSER_ULT_TIMEOUT_MS ms）。
 * 距离计算公式：distMm = 应答高字节 * 256 + 应答低字节（单位 mm）。
 * distMm : 输出参数，NULL 时仅更新 senserInfo，不写出；
 * 返回值  : 成功（CRC 通过且帧格式正确）返回 1，超时或校验失败返回 0。
 */
uint8_t DRIVER_SENSER_GetDistance(uint16_t *distMm){
  uint16_t crc, rspCrc;
  uint32_t startMs;

  /* 清除上次应答状态，准备接收新帧 */
  ultRspState.cnt   = 0U;
  ultRspState.ready = 0U;

  /* 发送 Modbus RTU 请求帧 */
  SENSER_ULT_DEP_UART_SEND_RAW(ultReqFrame, ULT_REQ_LEN);

  /* 阻塞等待应答帧接收完成 */
  startMs = HAL_GetTick();
  while(ultRspState.ready == 0U){
    if((HAL_GetTick() - startMs) >= SENSER_ULT_TIMEOUT_MS){
      return 0U;
    }
  }

  /* 校验 CRC16（覆盖应答帧前 5 字节，即不含 CRC 的部分）*/
  crc    = __DRIVER_SENSER_Crc16Calc(ultRspState.buf, ULT_RSP_LEN - 2U);
  rspCrc = (uint16_t)ultRspState.buf[ULT_RSP_CRCL_IDX] |
           ((uint16_t)ultRspState.buf[ULT_RSP_CRCH_IDX] << 8U);
  if(crc != rspCrc){
    return 0U;
  }

  /* 校验设备地址、功能码与字节数 */
  if(ultRspState.buf[0]          != SENSER_ULT_DEVICE_ADDR ||
     ultRspState.buf[ULT_RSP_FC_IDX] != 0x03U              ||
     ultRspState.buf[ULT_RSP_BC_IDX] != 0x02U){
    return 0U;
  }

  /* 解析距离值并更新 senserInfo */
  senserInfo.distMm  = ((uint16_t)ultRspState.buf[ULT_RSP_DH_IDX] << 8U) |
                        (uint16_t)ultRspState.buf[ULT_RSP_DL_IDX];
  senserInfo.isValid = 1U;

  if(distMm != NULL){
    *distMm = senserInfo.distMm;
  }
  return 1U;
}

/* HC-SR04：发送 10us 触发脉冲，阻塞测量回波时间并计算距离。
 * 时序：TRIG 低 2us → TRIG 高 10us → TRIG 低 → 等待 ECHO 高 → 计时 → 等待 ECHO 低 → 计算距离。
 * 距离公式：distMm = echoUs × 17 / 100（声速 340m/s，单程距离）。
 * distMm : 输出参数，NULL 时仅更新 senserInfo；
 * 成功返回 1，等待超时返回 0。
 */
uint8_t DRIVER_SENSER_GetHCSR04Distance(uint16_t *distMm){
  uint32_t startCyc, echoStartCyc, echoEndCyc, elapsedUs;

  /* 确保 TRIG 初始为低，等待 2us */
  SENSER_HCSR04_DEP_TRIG_SET(GPIO_LEVEL_LOW);
  SENSER_HCSR04_DEP_DELAY_US(2U);

  /* 发送 10us 触发脉冲 */
  SENSER_HCSR04_DEP_TRIG_SET(GPIO_LEVEL_HIGH);
  SENSER_HCSR04_DEP_DELAY_US(10U);
  SENSER_HCSR04_DEP_TRIG_SET(GPIO_LEVEL_LOW);

  /* 等待 ECHO 上升沿（传感器开始发超声波） */
  startCyc = SENSER_HCSR04_DEP_GET_CYC();
  while(SENSER_HCSR04_DEP_ECHO_READ() == GPIO_LEVEL_LOW){
    if(SENSER_HCSR04_DEP_ELAPSED_US(startCyc, SENSER_HCSR04_START_TIMEOUT_US)){
      return 0U;
    }
  }

  /* 等待 ECHO 下降沿并计时（超声波返回） */
  echoStartCyc = SENSER_HCSR04_DEP_GET_CYC();
  while(SENSER_HCSR04_DEP_ECHO_READ() == GPIO_LEVEL_HIGH){
    if(SENSER_HCSR04_DEP_ELAPSED_US(echoStartCyc, SENSER_HCSR04_ECHO_TIMEOUT_US)){
      return 0U;
    }
  }
  echoEndCyc = SENSER_HCSR04_DEP_GET_CYC();

  /* 计算 ECHO 高电平持续时间（微秒） */
  elapsedUs = (echoEndCyc - echoStartCyc) / dwtInfo.cycPerUs;

  /* 换算距离：单程 = 总时间 / 2，距离(mm) = 时间(us) × 17 / 100 */
  senserInfo.hcsrDistMm  = (uint16_t)(elapsedUs * 17U / 100U);
  senserInfo.hcsrIsValid = 1U;

  if(distMm != NULL){
    *distMm = senserInfo.hcsrDistMm;
  }
  return 1U;
}

/* AHT10：触发一次测量，阻塞等待 SENSER_AHT10_MEAS_WAIT_MS ms 后读取 6 字节原始数据，
 * 解析温湿度并更新 senserInfo.aht10TempX100 和 senserInfo.aht10HumiX100。
 * 成功返回 1（状态字节忙位为 0），失败返回 0。
 */
  uint8_t  rxBuf[6];
uint8_t DRIVER_SENSER_AHT10Update(void){

  uint32_t rawHumi, rawTemp;

  /* 发送触发采集命令 0xAC 0x33 0x00 */
  SENSER_AHT10_DEP_WRITE(0xACU, aht10TrigData, 2U);
  SENSER_AHT10_DEP_DELAY_MS(SENSER_AHT10_MEAS_WAIT_MS);

  /* 读取 6 字节原始数据 */
  SENSER_AHT10_DEP_READ(rxBuf, 6U);

  /* 检查忙标志位（bit7=0 表示测量完成）*/
  if((rxBuf[0] & 0x80U) != 0U){
    return 0U;
  }

  /* 解析 20 位湿度原始值：rxBuf[1][19:12] rxBuf[2][11:4] rxBuf[3][7:4]→[3:0] */
  rawHumi = ((uint32_t)rxBuf[1] << 12U) |
            ((uint32_t)rxBuf[2] <<  4U) |
            ((uint32_t)rxBuf[3] >>  4U);

  /* 解析 20 位温度原始值：rxBuf[3][3:0]→[19:16] rxBuf[4][15:8] rxBuf[5][7:0] */
  rawTemp = (((uint32_t)rxBuf[3] & 0x0FU) << 16U) |
             ((uint32_t)rxBuf[4]           <<  8U) |
              (uint32_t)rxBuf[5];

  /* 换算温度：T(°C) = rawTemp / 2^20 * 200 - 50 */
  senserInfo.aht10Temp = (float)rawTemp / 1048576.0f * 200.0f - 50.0f;

  /* 换算湿度：H(%RH) = rawHumi / 2^20 * 100 */
  senserInfo.aht10Humi = (float)rawHumi / 1048576.0f * 100.0f;

  senserInfo.aht10IsValid = 1U;
  return 1U;
}

/* AHT10：返回最近一次测量温度（单位 °C），需先调用 DRIVER_SENSER_AHT10Update。 */
float DRIVER_SENSER_GetAHT10Temp(void){
  return senserInfo.aht10Temp;
}

/* AHT10：返回最近一次测量湿度（单位 %RH），需先调用 DRIVER_SENSER_AHT10Update。 */
float DRIVER_SENSER_GetAHT10Humi(void){
  return senserInfo.aht10Humi;
}
