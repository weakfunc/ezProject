#ifndef __DRIVER_SENSER_H__
#define __DRIVER_SENSER_H__

#include <stdint.h>
#include "stdlib_common.h"
#include "stdlib_tim.h"
#include "stdlib_dwt.h"
#include "stdlib_pid.h"

/*============================================================================
 * 向下依赖（driver层依赖的stdlib资源，task层无需关心）
 *============================================================================*/

/*----------------------------------------------------------------------------
 * 水加热模块
 *----------------------------------------------------------------------------*/
/* PWM 通道映射 */
#define SENSER_DEP_HEAT_CH1_PWM_CH           PWM_TIM2_CH1
#define SENSER_DEP_HEAT_CH2_PWM_CH           PWM_TIM2_CH2

/*----------------------------------------------------------------------------
 * 水位检测模块
 *----------------------------------------------------------------------------*/
/* GPIO ID 映射 */
#define SENSER_DEP_WATER_LVL_CH1_GPIO_ID     GPIO_ID_USER_IO_9
#define SENSER_DEP_WATER_LVL_CH2_GPIO_ID     GPIO_ID_USER_IO_10

/* 有水判断电平（低电平代表有水） */
#define SENSER_DEP_WATER_LVL_HAS_WATER_LEVEL GPIO_LEVEL_LOW

/*----------------------------------------------------------------------------
 * DS18B20 水温传感器
 *----------------------------------------------------------------------------*/
/* DQ 引脚 GPIO ID（对应 PA0） */
#define SENSER_DEP_DS18B20_GPIO_ID           GPIO_ID_USER_IO_ADC

/*----------------------------------------------------------------------------
 * 水温闭环控制（PID）
 *----------------------------------------------------------------------------*/
/* 占用 stdlib_pid 实例 ID */
#define SENSER_DEP_WATER_TEMP_PID_ID         (0U)

/*============================================================================
 * 向上提供（task层使用的API）
 *============================================================================*/

/*----------------------------------------------------------------------------
 * 水加热模块
 *----------------------------------------------------------------------------*/
/* 通道索引 */
#define SENSER_HEAT_CH_1                     (0U)
#define SENSER_HEAT_CH_2                     (1U)
#define SENSER_HEAT_CH_COUNT                 (2U)

/* 占空比范围（满占空比 1000） */
#define SENSER_HEAT_DUTY_MAX                 (20000U)
#define SENSER_HEAT_DUTY_DEFAULT             (10000U)

/* 水加热通道信息结构体 */
typedef struct {
  uint32_t duty;  /* 当前占空比 */
} senserHeatChInfo_t;

/*----------------------------------------------------------------------------
 * 水位检测模块
 *----------------------------------------------------------------------------*/
/* 通道索引 */
#define SENSER_WATER_LVL_CH_1                (0U)
#define SENSER_WATER_LVL_CH_2                (1U)
#define SENSER_WATER_LVL_CH_COUNT            (2U)

/* 水位检测通道信息结构体 */
typedef struct {
  uint8_t hasWater;  /* 是否有水：1=有水，0=无水 */
} senserWaterLvlChInfo_t;

/*----------------------------------------------------------------------------
 * DS18B20 水温传感器
 *----------------------------------------------------------------------------*/
/* 设备异常时的返回值 */
#define SENSER_DS18B20_TEMP_ERROR            (-999.0f)

/*----------------------------------------------------------------------------
 * 水温闭环控制
 *----------------------------------------------------------------------------*/
/* 目标温度有效范围（℃） */
#define SENSER_WATER_TEMP_TARGET_MIN         (25.0f)
#define SENSER_WATER_TEMP_TARGET_MAX         (50.0f)

/* 水温控制状态信息结构体 */
typedef struct {
  float targetTemp; /* 当前目标温度（℃） */
} senserWaterTempCtrlInfo_t;

/*----------------------------------------------------------------------------
 * 模块信息结构体
 *----------------------------------------------------------------------------*/
/* 传感器模块信息结构体，汇总所有子模块数据 */
typedef struct {
  senserHeatChInfo_t        heat[SENSER_HEAT_CH_COUNT];
  senserWaterLvlChInfo_t    waterLvl[SENSER_WATER_LVL_CH_COUNT];
  float                     ds18b20Temp;    /* DS18B20 最新温度（℃），异常时为 SENSER_DS18B20_TEMP_ERROR */
  senserWaterTempCtrlInfo_t waterTempCtrl;  /* 水温闭环控制状态 */
} senserInfo_t;

extern senserInfo_t senserInfo;

/*----------------------------------------------------------------------------
 * 公共 API
 *----------------------------------------------------------------------------*/
/* 传感器驱动初始化，设置加热模块默认占空比，ds18b20Temp初始化为错误值 */
void DRIVER_SENSER_Init(void);

/*----------------------------------------------------------------------------
 * 水加热模块 API
 *----------------------------------------------------------------------------*/
/* 设置指定水加热模块占空比，ch取值为SENSER_HEAT_CH_x，duty范围0~SENSER_HEAT_DUTY_MAX */
void DRIVER_SENSER_HeatSetDuty(uint8_t ch, uint32_t duty);

/*----------------------------------------------------------------------------
 * 水位检测模块 API
 *----------------------------------------------------------------------------*/
/* 更新水位检测状态，读取IO电平写入senserInfo，需周期调用 */
void DRIVER_SENSER_WaterLvlUpdate(void);

/* 获取指定水位检测模块是否有水，ch取值为SENSER_WATER_LVL_CH_x，返回1=有水，0=无水 */
uint8_t DRIVER_SENSER_WaterLvlGet(uint8_t ch);

/*----------------------------------------------------------------------------
 * DS18B20 水温传感器 API
 *----------------------------------------------------------------------------*/
/* 初始化DS18B20，写配置寄存器将精度设为9位（转换时间约94ms） */
void DRIVER_SENSER_DS18B20Init(void);

/* 启动DS18B20温度转换并读取结果，返回摄氏度（float），设备异常返回SENSER_DS18B20_TEMP_ERROR。
 * 注意：内部调用vTaskDelay(100ms)，须在FreeRTOS任务中调用。 */
float DRIVER_SENSER_DS18B20GetTemp(void);

/*----------------------------------------------------------------------------
 * 水温闭环控制 API
 *----------------------------------------------------------------------------*/
/* 初始化水温PID控制器，需在DRIVER_SENSER_Init之后调用 */
void DRIVER_SENSER_WaterTempCtrlInit(void);

/* 以senserInfo.ds18b20Temp为反馈，计算PID并同步设置两路加热模块占空比。
 * targetTemp须在SENSER_WATER_TEMP_TARGET_MIN~MAX范围内。
 * 控制结果写入senserInfo.waterTempCtrl，须在DS18B20GetTemp之后周期调用。 */
void DRIVER_SENSER_WaterTempCtrlUpdate(float targetTemp);

#endif
