#ifndef __STDLIB_ADC_H__
#define __STDLIB_ADC_H__

#include "main.h"
#include <stdint.h>

/*============================================================================
 * 内部配置
 *============================================================================*/

/* ADC 参考电压（V）。 */
#define ADC_VREF            3.3f
/* ADC 满量程（12位分辨率）。 */
#define ADC_FULL_SCALE      4095.0f
/* 分压上桥电阻 R10（kΩ），与 R11 串联后接地。 */
#define ADC_R_TOP           20.0f
/* 分压下桥电阻 R11（kΩ），ADC 采样点在 R11 两端。 */
#define ADC_R_BOT           10.0f

/*============================================================================
 * API接口
 *============================================================================*/

/* ADC 采样信息结构体。 */
typedef struct {
  uint16_t adcRaw;      /* ADC 原始采样值（0~4095） */
  float    battVoltage; /* 换算后的电池实际电压（V） */
} adcInfo_t;

/* ADC 公共信息，供上层直接读取。 */
extern adcInfo_t adcInfo;

/* 初始化 ADC，配置采样通道为 ADC1_IN0（PA0）并执行自校准。 */
void STDLIB_ADC_Init(void);

/* 触发一次 ADC 采样并更新 adcInfo 中的原始值与换算电压。 */
void STDLIB_ADC_Sample(void);

#endif
