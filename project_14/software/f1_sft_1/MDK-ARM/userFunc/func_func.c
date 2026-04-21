/*============================================================================
 * README
 *
 *============================================================================*/

#include "func_func.h"
#include "driver_steer.h"
#include "driver_mpu6050.h"
#include "stdlib_pid.h"

/*============================================================================
 * 内部配置（仅func_func模块内部使用）
 *============================================================================*/

/* PID实例ID分配 */
#define GIMBAL_PID_YAW_ID    (0U)  /* yaw轴PID实例索引 */
#define GIMBAL_PID_PITCH_ID  (1U)  /* pitch轴PID实例索引 */

/* 舵机输出角度限幅（度） */
#define GIMBAL_SERVO_MIN_DEG  (10.0f)
#define GIMBAL_SERVO_MAX_DEG  (170.0f)

/* 数值限幅宏 */
#define GIMBAL_CLAMP(x, mn, mx)  ((x) < (mn) ? (mn) : ((x) > (mx) ? (mx) : (x)))

/*============================================================================
 * 公有变量
 *============================================================================*/

/* 云台控制信息（公有，供task层读取所有中间变量用于调试） */
funcInfo_t funcInfo;

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化云台：配置yaw/pitch两路PID实例，初始化MPU6050 */
void Gimbal_Init(void){
  funcInfo.yawTarget   = 90.0f;
  funcInfo.pitchTarget = 90.0f;
  funcInfo.yawU        = 0.0f;
  funcInfo.pitchU      = 0.0f;
  funcInfo.servoYaw    = 90.0f;
  funcInfo.servoPitch  = 90.0f;

  /* yaw PID：Kp=1.0, Ki=0.0, Kd=0.1，P限幅±20°，I限幅±10°，输出限幅±30° */
  STDLIB_PID_Init(GIMBAL_PID_YAW_ID,   1.0f, 0.0f, 0.1f, 20.0f, 10.0f, 30.0f);
  /* pitch PID：参数同yaw */
  STDLIB_PID_Init(GIMBAL_PID_PITCH_ID, 1.0f, 0.0f, 0.1f, 20.0f, 10.0f, 30.0f);

  DRIVER_MPU6050_Init();
}

/* 设置云台目标角度 */
void Gimbal_SetTarget(float yaw_deg, float pitch_deg){
  funcInfo.yawTarget   = yaw_deg;
  funcInfo.pitchTarget = pitch_deg;
}

/* 10ms周期驱动：读取IMU角度反馈，PID计算补偿量，输出舵机指令 */
void Gimbal_Task(void){
  /* 更新MPU6050传感器数据 */
  DRIVER_MPU6050_Update();

  /* 位置式PID：误差 = 目标 - 反馈，输出补偿量 */
  funcInfo.yawU   = STDLIB_PID_Calc(GIMBAL_PID_YAW_ID,
                                    funcInfo.yawTarget,
                                    mpu6050Info.angle.yawDeg);
  funcInfo.pitchU = STDLIB_PID_Calc(GIMBAL_PID_PITCH_ID,
                                    funcInfo.pitchTarget,
                                    mpu6050Info.angle.pitchDeg);

  /* 舵机指令 = 目标角度 + 补偿量，限幅 [10°, 170°] */
  funcInfo.servoYaw   = GIMBAL_CLAMP(funcInfo.yawTarget   + funcInfo.yawU,
                                     GIMBAL_SERVO_MIN_DEG, GIMBAL_SERVO_MAX_DEG);
  funcInfo.servoPitch = GIMBAL_CLAMP(funcInfo.pitchTarget + funcInfo.pitchU,
                                     GIMBAL_SERVO_MIN_DEG, GIMBAL_SERVO_MAX_DEG);

  DRIVER_STEER_SetAngleDeg(STEER_SERVO_1, funcInfo.servoYaw);
  DRIVER_STEER_SetAngleDeg(STEER_SERVO_2, funcInfo.servoPitch);
}
