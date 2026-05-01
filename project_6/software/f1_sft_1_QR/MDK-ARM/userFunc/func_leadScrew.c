/*============================================================================
 * README
 * 丝杆机构功能模块
 * 使用 driver_stepperMotor（地址 LEADSCRW_MOTOR_ID）驱动滑动丝杆
 * 导程 LEADSCRW_PITCH_MM mm/圈；顺时针（速度>0）为负方向运动
 * 提供绝对位置控制、周期状态查询、原点标定三类API
 *============================================================================*/

#include "func_leadScrew.h"

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* 停止判定阈值（realSpd_01rpm 单位 0.1RPM，10 对应 1RPM）。 */
#define LEADSCRW_STOP_SPD_THRESH  10

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 当前位置（cm，相对零点，含符号）。 */
static float sCurrentPos_cm;

/* 目标位置（cm，相对零点，含符号）。 */
static float sTargetPos_cm;

/* 当前运行状态。 */
static leadScrewState_e sState;

/* 电机起转标志：命令发出后需等到速度首次超阈值，才允许以速度归零判定停止。 */
static uint8_t sMotorStarted;

/*============================================================================
 * 对外变量
 *============================================================================*/

leadScrewInfo_t leadScrewInfo;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 将内部状态同步到公共 leadScrewInfo 结构体。 */
static void __LEADSCRW_SyncInfo(void) {
  leadScrewInfo.state         = sState;
  leadScrewInfo.currentPos_cm = sCurrentPos_cm;
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化丝杆模块，清零内部状态。 */
void FUNC_LEADSCREW_Init(void) {
  sCurrentPos_cm = 0.0f;
  sTargetPos_cm  = 0.0f;
  sState         = LEADSCRW_STATE_IDLE;
  sMotorStarted  = 0U;
  __LEADSCRW_SyncInfo();

  FUNC_LEADSCREW_CalibrateOrigin();
}

/* 丝杆绝对位置控制：移动至相对原点的指定位置。
 * targetCm: 目标位置（cm），正值为正方向，负值为负方向
 */
void FUNC_LEADSCREW_SetAbsPos(float targetCm) {
  float    delta;
  float    absDelta;
  float    revs_f;
  uint32_t revs;
  int16_t  speed;

  /* 运行中忽略新指令，防止叠加相对运动 */
  if(sState == LEADSCRW_STATE_RUNNING) {
    return;
  }

  delta    = targetCm - sCurrentPos_cm;
  absDelta = (delta < 0.0f) ? (-delta) : delta;

  /* 1圈 = LEADSCRW_PITCH_MM mm，1cm = 10mm，revs = cm * 10 / pitch */
  revs_f = absDelta * 10.0f / LEADSCRW_PITCH_MM;
  revs   = (uint32_t)(revs_f + 0.5f);

  /* 圆整后圈数为0则视为已到达，直接保持 IDLE */
  if(revs == 0U) {
    sCurrentPos_cm = targetCm;
    __LEADSCRW_SyncInfo();
    return;
  }

  /* 正方向（delta>0）→ 电机逆时针（速度<0）；负方向 → 顺时针（速度>0） */
  speed = (delta > 0.0f) ? (int16_t)(-LEADSCRW_DEFAULT_SPEED_RPM)
                         : (int16_t)( LEADSCRW_DEFAULT_SPEED_RPM);

  sTargetPos_cm = targetCm;
  sState        = LEADSCRW_STATE_RUNNING;
  sMotorStarted = 0U;

  DRIVER_STEPPER_RotateRevs(LEADSCRW_MOTOR_ID, speed, LEADSCRW_DEFAULT_ACC, revs);
  __LEADSCRW_SyncInfo();
}

/* 丝杆相对距离控制：在当前位置基础上移动指定距离。
 * deltaCm: 移动距离（cm），正值为正方向，负值为负方向
 */
void FUNC_LEADSCREW_SetRelPos(float deltaCm) {
  /* 运行中忽略新指令 */
  if(sState == LEADSCRW_STATE_RUNNING) {
    return;
  }
  FUNC_LEADSCREW_SetAbsPos(sCurrentPos_cm + deltaCm);
}

/* 丝杆状态查询（须周期调用）：以电机实时转速判断运动状态，更新 leadScrewInfo。
 * 速度超过阈值：RUNNING，标记已起转；
 * 速度回落且已起转过：IDLE，更新当前位置；
 * 速度为零但未曾起转（加速阶段）：不改变状态，避免误判停止。
 */
void FUNC_LEADSCREW_Update(void) {
  int16_t spd = STEPPER_INFO(LEADSCRW_MOTOR_ID).realSpd_01rpm;

  if(spd > LEADSCRW_STOP_SPD_THRESH || spd < -LEADSCRW_STOP_SPD_THRESH) {
    sState        = LEADSCRW_STATE_RUNNING;
    sMotorStarted = 1U;
  } else if(sMotorStarted) {
    /* 速度已经起来过，现在归零：运动完成 */
    if(sState == LEADSCRW_STATE_RUNNING) {
      sCurrentPos_cm = sTargetPos_cm;
    }
    sState        = LEADSCRW_STATE_IDLE;
    sMotorStarted = 0U;
  }
  /* 速度为零且 sMotorStarted==0：电机尚未起转，保持当前状态不变 */

  __LEADSCRW_SyncInfo();
}

/* 丝杆原点标定：将当前丝杆位置定义为零点。 */
void FUNC_LEADSCREW_CalibrateOrigin(void) {
  sCurrentPos_cm = 0.0f;
  sTargetPos_cm  = 0.0f;
  sState         = LEADSCRW_STATE_IDLE;
  sMotorStarted  = 0U;
  __LEADSCRW_SyncInfo();
}
