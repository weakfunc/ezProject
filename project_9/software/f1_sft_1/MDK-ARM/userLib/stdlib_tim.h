#ifndef __STDLIB_TIM_H__
#define __STDLIB_TIM_H__

#include "main.h"
#include <stdint.h>

/* PWM 通道到 HAL 定时器通道的映射项。 */
typedef struct {
    TIM_HandleTypeDef *htim;
    uint32_t           channel;
} pwmChMap_t;

/* PWM 通道总数。 */
#define PWM_CH_COUNT    12U

/* TIM2: 1 MHz 计数时钟，20 ms 周期，适合 50 Hz PWM。 */
#define PWM_TIM2_CH1    0U
#define PWM_TIM2_CH2    1U
#define PWM_TIM2_CH3    2U    /* 预留 */
#define PWM_TIM2_CH4    3U    /* 预留 */

/* TIM3: 1 MHz 计数时钟，1 ms 周期，适合 1 kHz PWM。 */
#define PWM_TIM3_CH1    4U
#define PWM_TIM3_CH2    5U
#define PWM_TIM3_CH3    6U
#define PWM_TIM3_CH4    7U

/* TIM4: 1 MHz 计数时钟，1 ms 周期，适合 1 kHz PWM。 */
#define PWM_TIM4_CH1    8U
#define PWM_TIM4_CH2    9U    /* 预留 */
#define PWM_TIM4_CH3    10U   /* 预留 */
#define PWM_TIM4_CH4    11U

/* 初始化并启动所有已注册的 PWM 通道。 */
void STDLIB_TIM_PwmInit(void);

/* 设置指定 PWM 通道的占空比，范围为 0.0 到 100.0。 */
void STDLIB_TIM_PwmSetDuty(uint8_t ch, float duty);

/* 读取指定 PWM 通道当前占空比，返回百分比。 */
float STDLIB_TIM_PwmGetDuty(uint8_t ch);

void STDLIB_TIM_PwmSetPulse(uint8_t ch, uint32_t pulse);
#endif
