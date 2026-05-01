#ifndef __DRIVER_SENSER_H__
#define __DRIVER_SENSER_H__

#include "stdlib_common.h"
#include "stdlib_dwt.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 * 依赖：stdlib_common, stdlib_dwt
 *============================================================================*/

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

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/

/* ---- HC-SR04 普通超声波 ---- */

/* 等待 ECHO 上升沿超时（微秒）。 */
#define SENSER_HCSR04_START_TIMEOUT_US  (500U)
/* ECHO 高电平最长持续时间（微秒）。 */
#define SENSER_HCSR04_ECHO_TIMEOUT_US   (38000U)

/* 传感器模块信息结构体。 */
typedef struct {
  uint16_t hcsrDistMm;   /* 最近一次有效测量距离（mm） */
  uint8_t  hcsrIsValid;  /* 距离数据是否有效（1：有效；0：无效） */
} senserInfo_t;

extern senserInfo_t senserInfo;

/* 初始化传感器驱动（TRIG 引脚复位）。 */
void DRIVER_SENSER_Init(void);

/* HC-SR04：发送 10us 触发脉冲，阻塞测量回波时间并计算距离。
 * distMm : 输出参数，NULL 时仅更新 senserInfo；
 * 成功返回 1，超时返回 0。
 */
uint8_t DRIVER_SENSER_GetHCSR04Distance(uint16_t *distMm);

#endif
