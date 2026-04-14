/* [新增] driver_adc.h — ADC电池电量驱动，框架原无此模块，本次新增 */
#ifndef __DRIVER_ADC_H__
#define __DRIVER_ADC_H__

#include <stdint.h>
#include "stdlib_adc.h"

/*============================================================================
 * 向下依赖（driver层向stdlib层索要）
 * 依赖：stdlib_adc — adcInfo.battVoltage（已由system任务周期采样）
 *============================================================================*/

/* 电池满电电压（V），对应100%电量
 * 2S锂电满电8.4V，硬件分压约5:1（stdlib按3:1换算），实际读数≈8.4×0.6=5.04V */
#define ADC_DEP_VBAT_FULL       (5.04f)
/* 电池空电电压（V），对应0%电量
 * 2S锂电截止6.0V，实际读数≈6.0×0.6=3.60V */
#define ADC_DEP_VBAT_EMPTY      (3.60f)

/*============================================================================
 * 向上提供（公有结构体、API）
 *============================================================================*/

/**
 * ADC驱动模块信息结构体
 * 管理从stdlib_adc换算得到的电量百分比
 */
typedef struct {
  uint8_t battPercent; /* 电池电量百分比（0~100） */
} adcDriverInfo_t;

extern adcDriverInfo_t adcDriverInfo;

/* 初始化ADC驱动模块，清零电量字段 */
void DRIVER_ADC_Init(void);

/* 读取已由系统任务采样的电池电压，换算为百分比，更新adcDriverInfo.battPercent */
void DRIVER_ADC_UpdateBattPercent(void);

#endif
