#include "stdlib_adc.h"
#include "adc.h"

/*============================================================================
 * 公有变量
 *============================================================================*/

/* ADC 采样信息，供上层直接读取。 */
adcInfo_t adcInfo;

/*============================================================================
 * API接口实现
 *============================================================================*/

/* 初始化 ADC：执行自校准，并将采样时间延长以满足分压网络源阻抗要求。
 * 需在 MX_ADC1_Init() 之后调用。
 * 采样通道：ADC1_IN0（PA0）。
 * 分压网络等效源阻抗：R10‖R11 ≈ 6.43kΩ；
 * CubeMX 默认的 1.5 周期远不够，重配为 55.5 周期（约 4.6µs @ 12MHz）保证精度。
 */
void STDLIB_ADC_Init(void){
  ADC_ChannelConfTypeDef sConfig = {0};

  /* 自校准须在任意转换前完成，校准结果与采样时间无关。 */
  HAL_ADCEx_Calibration_Start(&hadc1);

  /* 保持 IN1 通道不变，仅将采样时间延长至 55.5 周期。 */
  sConfig.Channel      = ADC_CHANNEL_0;
  sConfig.Rank         = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);

  adcInfo.adcRaw      = 0U;
  adcInfo.battVoltage = 0.0f;
}

/* 触发一次软件启动 ADC 采样，阻塞等待转换完成后更新 adcInfo。
 * 电压换算公式：Vbat = adcRaw * (Vref / FullScale) * (R_TOP + R_BOT) / R_BOT
 */
void STDLIB_ADC_Sample(void){
  HAL_ADC_Start(&hadc1);
  if(HAL_ADC_PollForConversion(&hadc1, 10U) == HAL_OK){
    adcInfo.adcRaw      = (uint16_t)HAL_ADC_GetValue(&hadc1);
    adcInfo.battVoltage = (float)adcInfo.adcRaw
                          * (ADC_VREF / ADC_FULL_SCALE)
                          * ((ADC_R_TOP + ADC_R_BOT) / ADC_R_BOT);
  }
  HAL_ADC_Stop(&hadc1);
}
