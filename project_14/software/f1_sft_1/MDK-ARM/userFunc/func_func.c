/*============================================================================
 * README
 * 功能层：二轴云台控制逻辑
 * 基于 driver_stepperMotor 提供的基础 API，封装云台级别的运动控制接口
 * 向 TASK 层提供 FUNC_GIMBAL_* 系列函数
 *============================================================================*/

#include "func_func.h"
#include "driver_stepperMotor.h"

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 轮询计数器，用于交替读取两轴反馈数据。 */
static uint8_t sUpdateCnt;

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化云台：初始化步进电机驱动并使能两轴电机。
 * 注意：调用方（task层）应在此函数后延时 ≥200ms，保证电机完成使能应答。
 */
void FUNC_GIMBAL_Init(void) {
  DRIVER_STEPPER_Init();
  DRIVER_STEPPER_Enable(STEPPER_AXIS_PITCH, 1U, STEPPER_SYNC_NOW);
  DRIVER_STEPPER_Enable(STEPPER_AXIS_YAW,   1U, STEPPER_SYNC_NOW);
  sUpdateCnt = 0U;
}

/* 设置俯仰轴目标角度（梯形加减速绝对位置模式）。
 * 正角度=CW方向，负角度=CCW方向，角度值转换为 0.1° 单位后发送。
 */
void FUNC_GIMBAL_SetPitchAngle(float pitchDeg, uint16_t speed,
                               uint16_t accAccel, uint16_t decAccel) {
  uint8_t  dir;
  uint32_t pos;

  if(pitchDeg >= 0.0f) {
    dir = STEPPER_DIR_CW;
    pos = (uint32_t)(pitchDeg * 10.0f + 0.5f);
  } else {
    dir = STEPPER_DIR_CCW;
    pos = (uint32_t)(-pitchDeg * 10.0f + 0.5f);
  }
  DRIVER_STEPPER_SetPosTrapezoid(STEPPER_AXIS_PITCH, dir, accAccel, decAccel,
                                 speed, pos, STEPPER_MODE_ABS, STEPPER_SYNC_NOW);
}

/* 设置偏航轴目标角度（梯形加减速绝对位置模式）。
 * 正角度=CW方向，负角度=CCW方向，角度值转换为 0.1° 单位后发送。
 */
void FUNC_GIMBAL_SetYawAngle(float yawDeg, uint16_t speed,
                             uint16_t accAccel, uint16_t decAccel) {
  uint8_t  dir;
  uint32_t pos;

  if(yawDeg >= 0.0f) {
    dir = STEPPER_DIR_CW;
    pos = (uint32_t)(yawDeg * 10.0f + 0.5f);
  } else {
    dir = STEPPER_DIR_CCW;
    pos = (uint32_t)(-yawDeg * 10.0f + 0.5f);
  }
  DRIVER_STEPPER_SetPosTrapezoid(STEPPER_AXIS_YAW, dir, accAccel, decAccel,
                                 speed, pos, STEPPER_MODE_ABS, STEPPER_SYNC_NOW);
}

/* 触发两轴无限位碰撞回零。 */
void FUNC_GIMBAL_Home(void) {
  DRIVER_STEPPER_GoHome(STEPPER_AXIS_PITCH, 2U, STEPPER_SYNC_NOW);
  DRIVER_STEPPER_GoHome(STEPPER_AXIS_YAW,   2U, STEPPER_SYNC_NOW);
}

/* 立即停止两轴。 */
void FUNC_GIMBAL_Stop(void) {
  DRIVER_STEPPER_Stop(STEPPER_AXIS_PITCH, STEPPER_SYNC_NOW);
  DRIVER_STEPPER_Stop(STEPPER_AXIS_YAW,   STEPPER_SYNC_NOW);
}

/* 轮询两轴反馈：每次调用依次请求位置/状态，4次完成一轮。 */
void FUNC_GIMBAL_Update(void) {
  switch(sUpdateCnt & 0x03U) {
    case 0U: DRIVER_STEPPER_ReadPos(STEPPER_AXIS_PITCH);    break;
    case 1U: DRIVER_STEPPER_ReadStatus(STEPPER_AXIS_PITCH); break;
    case 2U: DRIVER_STEPPER_ReadPos(STEPPER_AXIS_YAW);      break;
    case 3U: DRIVER_STEPPER_ReadStatus(STEPPER_AXIS_YAW);   break;
    default: break;
  }
  sUpdateCnt++;
}
