/* [新增] driver_adc.c — ADC电池电量驱动，框架原无此模块，本次新增 */
#include "driver_adc.h"

/*============================================================================
 * 内部配置
 *============================================================================*/

/* IIR低通滤波系数α：新样本权重，范围(0,1)。
 * α=0.1 对应约4.5个采样周期（500ms×4.5≈2.25s）的时间常数，
 * 足以滤除电源纹波和ADC量化噪声，同时电池真实电量变化（数分钟量级）不受影响。 */
#define ADC_FILTER_ALPHA      (0.1f)

/*============================================================================
 * 私有变量
 *============================================================================*/

/* IIR滤波器状态：滤波后的电池电压（V） */
static float   adcVoltFiltered;
/* 滤波器初始化标志：首次采样直接赋值而不从0收敛 */
static uint8_t adcFilterInited;

/*============================================================================
 * 公有变量
 *============================================================================*/

/* ADC驱动模块数据 */
adcDriverInfo_t adcDriverInfo;

/*============================================================================
 * API接口实现
 *============================================================================*/

/**
 * @brief  初始化ADC驱动模块，清零电量字段
 * @note   STDLIB_ADC_Init()已在system任务中调用，此处不重复初始化硬件
 */
void DRIVER_ADC_Init(void) {
  adcDriverInfo.battPercent = 0U;
  adcVoltFiltered           = 0.0f;
  adcFilterInited           = 0U;
}

/**
 * @brief  读取已由system任务采样的电池电压，换算为0~100%电量百分比
 * @note   换算公式：percent = (Vbat - 3.3) / (4.2 - 3.3) * 100，限幅0~100
 * @note   STDLIB_ADC_Sample()已在system任务中每500ms调用，此处仅读取结果
 * @note   硬件分压：R10=20kΩ / R11=10kΩ，stdlib已将ADC值换算为实际电压
 *         3.7V锂电：满电4.2V=100%，空电3.3V=0%
 */
void DRIVER_ADC_UpdateBattPercent(void) {
  float   vbat = adcInfo.battVoltage;
  float   filtVbat;
  int16_t pct;

  /* IIR低通滤波：首次调用直接用原始值初始化，避免从0.0V缓慢收敛 */
  if(adcFilterInited == 0U) {
    adcVoltFiltered = vbat;
    adcFilterInited = 1U;
  } else {
    adcVoltFiltered = adcVoltFiltered * (1.0f - ADC_FILTER_ALPHA)
                      + vbat * ADC_FILTER_ALPHA;
  }
  filtVbat = adcVoltFiltered;

  /* 用滤波后电压换算百分比，限幅0~100 */
  if(filtVbat >= ADC_DEP_VBAT_FULL) {
    pct = 100;
  } else if(filtVbat <= ADC_DEP_VBAT_EMPTY) {
    pct = 0;
  } else {
    pct = (int16_t)((filtVbat - ADC_DEP_VBAT_EMPTY)
                    / (ADC_DEP_VBAT_FULL - ADC_DEP_VBAT_EMPTY) * 100.0f);
  }
  adcDriverInfo.battPercent = (uint8_t)pct;
}
