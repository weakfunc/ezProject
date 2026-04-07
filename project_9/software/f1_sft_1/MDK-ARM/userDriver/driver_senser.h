#ifndef __DRIVER_SENSER_H__
#define __DRIVER_SENSER_H__

#include "stdlib_usart.h"
#include "stdlib_common.h"
#include "stdlib_dwt.h"
#include "stdlib_i2c.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 * 依赖：stdlib_usart, stdlib_common, stdlib_dwt, stdlib_i2c
 *============================================================================*/

/* ---- RS485 高精度超声波 (HL-ULT1) ---- */

/* 使用的串口端口（UART_PORT2，自定义协议）。 */
#define SENSER_ULT_DEP_UART_PORT              UART_PORT2
/* 发送原始字节流（Modbus RTU 请求帧）。 */
#define SENSER_ULT_DEP_UART_SEND_RAW(d, l)   STDLIB_USART_SendRaw(SENSER_ULT_DEP_UART_PORT, (d), (l))
/* 注册字节回调。 */
#define SENSER_ULT_DEP_UART_SET_CB(cb)        STDLIB_USART_SetCustomCb(SENSER_ULT_DEP_UART_PORT, (cb))

/* ---- HC-SR04 普通超声波 ---- */

/* TRIG 引脚（USER_IO_1，GPIO 输出）。 */
#define SENSER_HCSR04_DEP_TRIG_GPIO_ID        GPIO_ID_USER_IO_1
/* ECHO 引脚（USER_IO_2，需在 CubeMX 中配置为 GPIO Input）。 */
#define SENSER_HCSR04_DEP_ECHO_GPIO_ID        GPIO_ID_USER_IO_2
/* 控制 TRIG 电平。 */
#define SENSER_HCSR04_DEP_TRIG_SET(level)     STDLIB_COMMON_GpioWrite(SENSER_HCSR04_DEP_TRIG_GPIO_ID, (level))
/* 读取 ECHO 电平。 */
#define SENSER_HCSR04_DEP_ECHO_READ()         STDLIB_COMMON_GpioRead(SENSER_HCSR04_DEP_ECHO_GPIO_ID)
/* 微秒阻塞延时。 */
#define SENSER_HCSR04_DEP_DELAY_US(us)        STDLIB_DWT_DelayUs(us)
/* 获取当前 DWT 周期计数。 */
#define SENSER_HCSR04_DEP_GET_CYC()           STDLIB_DWT_GetCyc()
/* 判断自 startCyc 起是否已过 us 微秒。 */
#define SENSER_HCSR04_DEP_ELAPSED_US(s, us)   STDLIB_DWT_IsElapsedUs((s), (us))

/* ---- AHT10 温湿度传感器 ---- */

/* 使用的软件 I2C 总线。 */
#define SENSER_AHT10_DEP_I2C_BUS              I2C_BUS_1
/* I2C 写地址（0x38 << 1）。 */
#define SENSER_AHT10_DEP_WRITE_ADDR           (0x70U)
/* I2C 读地址（0x38 << 1 | 1）。 */
#define SENSER_AHT10_DEP_READ_ADDR            (0x71U)
/* 发送命令+数据。 */
#define SENSER_AHT10_DEP_WRITE(ctrl, d, l)    STDLIB_I2C_WriteData(SENSER_AHT10_DEP_I2C_BUS, SENSER_AHT10_DEP_WRITE_ADDR, (ctrl), (d), (l))
/* 读取原始数据。 */
#define SENSER_AHT10_DEP_READ(d, l)           STDLIB_I2C_ReadData(SENSER_AHT10_DEP_I2C_BUS, SENSER_AHT10_DEP_READ_ADDR, (d), (l))
/* 毫秒阻塞延时。 */
#define SENSER_AHT10_DEP_DELAY_MS(ms)         HAL_Delay(ms)

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/

/* ---- RS485 高精度超声波 (HL-ULT1) ---- */

/* Modbus 设备地址（出厂默认 1）。 */
#define SENSER_ULT_DEVICE_ADDR          (0x01U)
/* 读距离阻塞超时（毫秒）。 */
#define SENSER_ULT_TIMEOUT_MS           (100U)

/* ---- HC-SR04 普通超声波 ---- */

/* 等待 ECHO 上升沿超时（微秒）。 */
#define SENSER_HCSR04_START_TIMEOUT_US  (500U)
/* ECHO 高电平最长持续时间（微秒）。 */
#define SENSER_HCSR04_ECHO_TIMEOUT_US   (38000U)

/* ---- AHT10 温湿度传感器 ---- */

/* 初始化命令等待时间（毫秒）。 */
#define SENSER_AHT10_INIT_WAIT_MS       (75U)
/* 触发采集后等待时间（毫秒）。 */
#define SENSER_AHT10_MEAS_WAIT_MS       (80U)

/* 传感器模块信息结构体。 */
typedef struct {
  /* RS485 高精度超声波 (HL-ULT1) */
  uint16_t distMm;          /* 最近一次有效测量距离（mm） */
  uint8_t  isValid;         /* 距离数据是否有效（1：有效；0：无效） */
  /* HC-SR04 普通超声波 */
  uint16_t hcsrDistMm;      /* 最近一次有效测量距离（mm） */
  uint8_t  hcsrIsValid;     /* 距离数据是否有效（1：有效；0：无效） */
  /* AHT10 温湿度传感器 */
  float   aht10Temp;      /* 最近一次温度（°C，如 23.50） */
  float   aht10Humi;      /* 最近一次湿度（%RH，如 60.00） */
  uint8_t aht10IsValid;   /* 温湿度数据是否有效（1：有效；0：无效） */
} senserInfo_t;

extern senserInfo_t senserInfo;

/* 初始化传感器驱动（RS485 回调注册、TRIG 复位、AHT10 初始化命令）。 */
void DRIVER_SENSER_Init(void);

/* RS485 高精度超声波：发送 Modbus RTU 请求，阻塞等待响应（最长 SENSER_ULT_TIMEOUT_MS ms）。
 * distMm : 输出参数，NULL 时仅更新 senserInfo；
 * 成功返回 1，超时或校验失败返回 0。
 */
uint8_t DRIVER_SENSER_GetDistance(uint16_t *distMm);

/* HC-SR04：发送 10us 触发脉冲，阻塞测量回波时间并计算距离。
 * distMm : 输出参数，NULL 时仅更新 senserInfo；
 * 成功返回 1，超时返回 0。
 */
uint8_t DRIVER_SENSER_GetHCSR04Distance(uint16_t *distMm);

/* AHT10：触发一次测量并阻塞等待（约 80ms），更新 senserInfo 中的温湿度数据。
 * 成功返回 1（状态字节忙位为 0），失败返回 0。
 */
uint8_t DRIVER_SENSER_AHT10Update(void);

/* AHT10：返回最近一次测量的温度（单位 °C），需先调用 DRIVER_SENSER_AHT10Update。 */
float DRIVER_SENSER_GetAHT10Temp(void);

/* AHT10：返回最近一次测量的湿度（单位 %RH），需先调用 DRIVER_SENSER_AHT10Update。 */
float DRIVER_SENSER_GetAHT10Humi(void);

#endif
