#ifndef __FUNC_FUNC_H__
#define __FUNC_FUNC_H__

#include "driver_senser.h"
#include "driver_tb6612.h"
#include "stdlib_pid.h"

/*============================================================================
 * 向下依赖（func层依赖的stdlib和driver资源）
 *   stdlib: stdlib_pid（STDLIB_PID_Init, STDLIB_PID_Calc）
 *   driver: driver_senser（DRIVER_SENSER_HeatSetDuty, senserInfo.ds18b20Temp）
 *   driver: driver_tb6612（DRIVER_TB6612_MotorSetSpeed, DRIVER_TB6612_EncoderGetRpm）
 *============================================================================*/

/* 占用 stdlib_pid 实例 ID */
#define FUNC_FUNC_DEP_WATER_TEMP_PID_ID        (0U)
#define FUNC_FUNC_DEP_MOTOR_SPEED_PID_BASE_ID  (1U)  /* ID 1=电机0，ID 2=电机1 */

/*============================================================================
 * 向上提供
 *============================================================================*/

/*----------------------------------------------------------------------------
 * 水温闭环控制
 *----------------------------------------------------------------------------*/
/* 目标温度有效范围（℃） */
#define FUNC_FUNC_WATER_TEMP_TARGET_MIN   (25.0f)
#define FUNC_FUNC_WATER_TEMP_TARGET_MAX   (50.0f)

/* 水温控制状态信息结构体 */
typedef struct {
  float targetTemp; /* 当前目标温度（℃） */
} funcFuncWaterTempCtrlInfo_t;

/*----------------------------------------------------------------------------
 * 电机转速闭环控制
 *----------------------------------------------------------------------------*/
/* 电机转速控制状态信息结构体（每路电机一个元素） */
typedef struct {
  int16_t targetRpm; /* 期望转速（RPM），正值正转，负值反转 */
} funcFuncMotorSpeedCtrlInfo_t;

/*----------------------------------------------------------------------------
 * 模块信息结构体
 *----------------------------------------------------------------------------*/
/* func_func模块信息结构体，汇总所有子模块数据 */
typedef struct {
  funcFuncWaterTempCtrlInfo_t  waterTempCtrl;                       /* 水温闭环控制状态 */
  funcFuncMotorSpeedCtrlInfo_t motorSpeedCtrl[TB6612_MOTOR_COUNT];  /* 电机转速闭环控制状态 */
} funcFuncInfo_t;

extern funcFuncInfo_t funcFuncInfo;

/*----------------------------------------------------------------------------
 * 水温闭环控制 API
 *----------------------------------------------------------------------------*/
/* 初始化水温PID控制器，需在driver_senser初始化之后调用 */
void FUNC_FUNC_WaterTempCtrlInit(void);

/* 以senserInfo.ds18b20Temp为反馈，计算PID并同步设置两路加热模块占空比。
 * targetTemp须在FUNC_FUNC_WATER_TEMP_TARGET_MIN~MAX范围内。
 * 负输出置零（加热器只能加热，超温时停止输出，依靠自然冷却）。
 * 须在DRIVER_SENSER_DS18B20GetTemp()之后周期调用。 */
void FUNC_FUNC_WaterTempCtrlUpdate(float targetTemp);

/*----------------------------------------------------------------------------
 * 电机转速闭环控制 API
 *----------------------------------------------------------------------------*/
/* 初始化所有电机转速PID控制器，期望转速置零 */
void FUNC_FUNC_MotorSpeedCtrlInit(void);

/* 以DRIVER_TB6612_EncoderGetRpm()为反馈，对所有电机执行一次PID计算并调用MotorSetSpeed。
 * targetRpm：期望转速（RPM），正值正转，负值反转。
 * 须每10ms（与EncoderUpdate同周期）调用一次。 */
void FUNC_FUNC_MotorSpeedCtrlUpdate(int16_t targetRpm);

#endif
