#ifndef __FUNC_FUNC_H__
#define __FUNC_FUNC_H__

#include <stdint.h>

/*============================================================================
 * 云台控制 API
 * 基于 driver_stepperMotor 模块，向 TASK 层提供二轴云台控制接口。
 * 俯仰轴（PITCH）= STEPPER_AXIS_PITCH，偏航轴（YAW）= STEPPER_AXIS_YAW。
 *============================================================================*/

/* 初始化云台（初始化步进电机驱动，使能两轴电机）。 */
void FUNC_GIMBAL_Init(void);

/* 设置云台俯仰轴目标角度（绝对位置模式，相对回零后的坐标零点）。
 * pitchDeg : 目标角度（°），精度 0.1°；正数=CW，负数=CCW
 * speed    : 运行速度（0.1RPM），范围 0-30000（即 0-3000.0RPM）
 * accAccel : 加速加速度（RPM/S），范围 0-65535
 * decAccel : 减速加速度（RPM/S），范围 0-65535
 */
void FUNC_GIMBAL_SetPitchAngle(float pitchDeg, uint16_t speed,
                               uint16_t accAccel, uint16_t decAccel);

/* 设置云台偏航轴目标角度（绝对位置模式，相对回零后的坐标零点）。
 * yawDeg   : 目标角度（°），精度 0.1°；正数=CW，负数=CCW
 * speed    : 运行速度（0.1RPM），范围 0-30000（即 0-3000.0RPM）
 * accAccel : 加速加速度（RPM/S），范围 0-65535
 * decAccel : 减速加速度（RPM/S），范围 0-65535
 */
void FUNC_GIMBAL_SetYawAngle(float yawDeg, uint16_t speed,
                             uint16_t accAccel, uint16_t decAccel);

/* 触发两轴回零（无限位碰撞回零模式）。 */
void FUNC_GIMBAL_Home(void);

/* 紧急停止两轴。 */
void FUNC_GIMBAL_Stop(void);

/* 周期性状态更新，在任务中定期调用以轮询两轴反馈（位置、状态）。
 * 每次调用交替请求一个轴的一项数据，避免总线连续占用。
 */
void FUNC_GIMBAL_Update(void);

#endif
