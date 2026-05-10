#ifndef __DRIVER_SENSER_H__
#define __DRIVER_SENSER_H__

#include <stdint.h>
#include <stddef.h>
#include "stdlib_common.h"
#include "stdlib_tim.h"
#include "stdlib_dwt.h"

/*============================================================================
 * 底层依赖配置
 *============================================================================*/
/* HX711 使用两线同步时序，不是 I2C/SPI/UART。
 * DOUT/DT 为 HX711 数据输出，MCU 侧配置为输入；
 * PD_SCK/SCK 为 MCU 输出时钟，空闲时必须保持低电平。
 *
 * 默认接线：HX711 DOUT/DT -> PB11，PD_SCK/SCK -> PB10。
 * 如果 CubeMX 中换了引脚，只需要修改下面四个宏。
 */
#define SENSER_DEP_HX711_DOUT_GPIO_Port      GPIOB
#define SENSER_DEP_HX711_DOUT_Pin            GPIO_PIN_11
#define SENSER_DEP_HX711_PD_SCK_GPIO_Port    GPIOB
#define SENSER_DEP_HX711_PD_SCK_Pin          GPIO_PIN_10

/* HX711 读完 24bit 数据后继续输出的时钟数决定“下一次转换”的通道和增益：
 * 1 个时钟：通道 A，增益 128
 * 2 个时钟：通道 B，增益 32
 * 3 个时钟：通道 A，增益 64
 */
#define SENSER_HX711_GAIN_PULSE              (1U)
#define SENSER_HX711_DEFAULT_SCALE_COUNT_PER_G (431.7754f)
#define SENSER_HX711_DEFAULT_ZERO_OFFSET     (8672719)

typedef struct {
    float    currentWeightG;   /* 当前重量，单位 g，由 rawAdc 按校准参数换算得到 */
    uint32_t rawAdc;           /* 最近一次 HX711 原始 ADC 值，已按手册例程异或 0x800000 */
    int32_t  zeroOffset;       /* 空载零点 ADC 值，用于去皮/清零 */
    float    scaleCountPerG;   /* 每 1g 对应的 ADC 计数，需实物标定 */
    uint8_t  isReady;          /* DOUT 为低时置 1，表示 HX711 当前数据可读 */
    uint8_t  hasNewData;       /* 本次 InfoUpdate 是否成功读到新数据 */
    uint32_t updateCount;      /* 成功更新次数 */
    uint32_t notReadyCount;    /* 周期更新时发现 DOUT 未就绪的次数 */
    uint32_t lastUpdateTickMs; /* 最近一次成功更新的系统 tick */
} senserInfo_t;

extern senserInfo_t senserInfo;

/* 初始化称重驱动状态，并确保 PD_SCK 输出低电平。 */
void DRIVER_SENSER_Init(void);

/* 周期调用：若 HX711 数据就绪，则读取并更新 senserInfo.currentWeightG。 */
void DRIVER_SENSER_InfoUpdate(void);

/* 查询 HX711 是否就绪：DOUT=0 表示可读。 */
uint8_t DRIVER_SENSER_IsReady(void);

/* 读取一次 HX711 原始 ADC 值；未就绪返回 0，读取成功返回 1。 */
uint8_t DRIVER_SENSER_ReadRaw(uint32_t *rawAdc);

/* 获取最近一次缓存的重量值，不会触发新的硬件读取。 */
float DRIVER_SENSER_GetCurrentWeightG(void);

/* 设置校准参数：weightG = (rawAdc - zeroOffset) / scaleCountPerG。 */
void DRIVER_SENSER_SetCalibration(int32_t zeroOffset, float scaleCountPerG);

/* 将当前 rawAdc 作为零点，适合空载时调用。 */
void DRIVER_SENSER_SetZeroOffsetToCurrent(void);

#endif
