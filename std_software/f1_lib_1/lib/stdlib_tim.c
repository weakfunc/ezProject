#include "stdlib_tim.h"

/* PWM 通道到 HAL 句柄的映射表。 */
static const pwmChMap_t pwmMap[PWM_CH_COUNT] = {
    /* TIM2: 50 Hz, ARR = 19999 */
    [PWM_TIM2_CH1] = { &htim2, TIM_CHANNEL_1 },
    [PWM_TIM2_CH2] = { &htim2, TIM_CHANNEL_2 },

    /* TIM3: 1 kHz, ARR = 999 */
    [PWM_TIM3_CH1] = { &htim3, TIM_CHANNEL_1 },
    [PWM_TIM3_CH2] = { &htim3, TIM_CHANNEL_2 },
    [PWM_TIM3_CH3] = { &htim3, TIM_CHANNEL_3 },
    [PWM_TIM3_CH4] = { &htim3, TIM_CHANNEL_4 },

    /* TIM4: 1 kHz, ARR = 999 */
    [PWM_TIM4_CH1] = { &htim4, TIM_CHANNEL_1 },
    [PWM_TIM4_CH4] = { &htim4, TIM_CHANNEL_4 },
};

/* 启动所有在映射表中登记过的 PWM 通道。 */
void STDLIB_TIM_PwmInit(void){
    for(uint8_t i = 0U; i < PWM_CH_COUNT; i++){
        if(pwmMap[i].htim != NULL){
            HAL_TIM_PWM_Start(pwmMap[i].htim, pwmMap[i].channel);
        }
    }
}

/* 按百分比设置指定通道的占空比。 */
void STDLIB_TIM_PwmSetDuty(uint8_t ch, float duty){
    if(ch >= PWM_CH_COUNT || pwmMap[ch].htim == NULL) return;
    if(duty < 0.0f) duty = 0.0f;
    if(duty > 100.0f) duty = 100.0f;

    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(pwmMap[ch].htim);
    uint32_t ccr = (uint32_t)((duty / 100.0f) * (float)(arr + 1U));

    __HAL_TIM_SET_COMPARE(pwmMap[ch].htim, pwmMap[ch].channel, ccr);
}

/* 按装载值设置指定通道的占空比。 */
void STDLIB_TIM_PwmSetPulse(uint8_t ch, uint32_t pulse){
	if(ch >= PWM_CH_COUNT || pwmMap[ch].htim == NULL) return;
		
	uint32_t arr = __HAL_TIM_GET_AUTORELOAD(pwmMap[ch].htim) + 1U;
	uint32_t ccr = pulse;
	
	if(ccr >= arr) ccr = arr;

  __HAL_TIM_SET_COMPARE(pwmMap[ch].htim, pwmMap[ch].channel, ccr);
}

/* 读取指定通道当前的占空比百分比。 */
float STDLIB_TIM_PwmGetDuty(uint8_t ch){
    if(ch >= PWM_CH_COUNT || pwmMap[ch].htim == NULL) return 0.0f;

    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(pwmMap[ch].htim);
    uint32_t ccr = __HAL_TIM_GET_COMPARE(pwmMap[ch].htim, pwmMap[ch].channel);

    return ((float)ccr / (float)(arr + 1U)) * 100.0f;
}
