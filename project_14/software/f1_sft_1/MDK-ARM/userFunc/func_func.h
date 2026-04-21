#ifndef __FUNC_FUNC_H__
#define __FUNC_FUNC_H__

#include <stdint.h>

/*============================================================================
 * 向上提供
 *============================================================================*/

/* 云台控制信息结构体（公有，供task层读取所有中间变量用于调试） */
typedef struct {
  float yawTarget;   /* 目标yaw角度（度） */
  float pitchTarget; /* 目标pitch角度（度） */
  float yawU;        /* yaw轴PID补偿量（度） */
  float pitchU;      /* pitch轴PID补偿量（度） */
  float servoYaw;    /* yaw轴舵机输出指令（度） */
  float servoPitch;  /* pitch轴舵机输出指令（度） */
} funcInfo_t;

extern funcInfo_t funcInfo;

/* 初始化云台：配置PID实例并初始化MPU6050 */
void Gimbal_Init(void);

/* 设置云台目标角度（度） */
void Gimbal_SetTarget(float yaw_deg, float pitch_deg);

/* 放入10ms周期子任务调用，读取IMU反馈，执行PID，驱动舵机 */
void Gimbal_Task(void);

#endif
