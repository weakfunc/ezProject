/*============================================================================
 * README
 * Two-axis gimbal control module.
 * The task layer calls FUNC_GAMBAL_Updata() every 2ms; this module sends
 * concrete stepper control commands from the stepper weak hooks.
 *============================================================================*/

#include "func_gambal.h"
#include "driver_stepperMotor.h"
#include "driver_verison.h"
#include <string.h>

/*============================================================================
 * Private Macros
 *============================================================================*/

#define GAMBAL_POS_VEL_RPM             3000
#define GAMBAL_POS_ACC                 150U
#define GAMBAL_PULSE_PER_DEG           ((float)STEPPER_PULSE_PER_REV / 360.0f)

#define GAMBAL_YAW_MIN_DEG             (-120.0f)
#define GAMBAL_YAW_MAX_DEG             120.0f
#define GAMBAL_PITCH_MIN_DEG           (-90.0f)
#define GAMBAL_PITCH_MAX_DEG           90.0f

/* 2ms control period low-pass coefficient for discontinuous camera inputs. */
#define GAMBAL_REF_FILTER_ALPHA        0.15f

/* Send current gimbal angle to camera every 10ms. */
#define GAMBAL_CAMERA_TX_PERIOD_CNT    5U

/* Axis sign convention:
 * - yaw   : clockwise rotation is positive; current motor command direction is reversed.
 * - pitch : clockwise rotation is negative; current motor command direction is already correct.
 */
#define GAMBAL_PITCH_DIR               1.0f
#define GAMBAL_YAW_DIR                 (-1.0f)

/*============================================================================
 * Private Types
 *============================================================================*/

typedef struct {
  float   refYaw_deg;
  float   refPitch_deg;
  int32_t yawPulse;
  int32_t pitchPulse;
  uint8_t posMode;
  uint8_t filterReady;
  uint8_t cameraTxCnt;
} gambalCtrlInfo_t;

/*============================================================================
 * Private Variables
 *============================================================================*/

gambalCtrlInfo_t sGambalCtrlInfo;

/*============================================================================
 * Private Functions
 *============================================================================*/

static int32_t __GAMBAL_AngleToPulse(float angle_deg) {
  float pulse = angle_deg * GAMBAL_PULSE_PER_DEG;

  if(pulse >= 0.0f) {
    return (int32_t)(pulse + 0.5f);
  }
  return (int32_t)(pulse - 0.5f);
}

static float __GAMBAL_Limit(float value, float min, float max) {
  if(value < min) {
    return min;
  }
  if(value > max) {
    return max;
  }
  return value;
}

static float __GAMBAL_LowPass(float last, float input) {
  return last + (input - last) * GAMBAL_REF_FILTER_ALPHA;
}

static void __GAMBAL_WriteFloatLE(uint8_t *data, float value) {
  memcpy(data, &value, sizeof(value));
}

static float __GAMBAL_GetFbdAxisAngle(uint8_t motorId, float dir) {
  return STEPPER_INFO(motorId).fbdPosRevs * 360.0f * dir;
}

static void __GAMBAL_SetPos(uint8_t motorId, int32_t pulse) {
  int16_t  vel = (int16_t)GAMBAL_POS_VEL_RPM;
  uint32_t clk = (uint32_t)pulse;

  if(pulse < 0) {
    vel = (int16_t)(-GAMBAL_POS_VEL_RPM);
    clk = (uint32_t)(-pulse);
  }

  DRIVER_STEPPER_SetPos(motorId, vel, GAMBAL_POS_ACC, clk,
                        sGambalCtrlInfo.posMode);
}

/*============================================================================
 * Weak Hook Overrides
 *============================================================================*/

void DRIVER_STEPPER_Axis0CtrlHook(void) {
  __GAMBAL_SetPos(STEPPER_ADDR_PITCH, sGambalCtrlInfo.pitchPulse);
}

void DRIVER_STEPPER_Axis1CtrlHook(void) {
  __GAMBAL_SetPos(STEPPER_ADDR_YAW, sGambalCtrlInfo.yawPulse);
}

void DRIVER_STEPPER_CommonCtrlHook(void) {
}

/*============================================================================
 * APIs
 *============================================================================*/

void FUNC_GAMBAL_Init(void) {
  sGambalCtrlInfo.posMode = STEPPER_MODE_ABS;
  sGambalCtrlInfo.filterReady = 0U;
  sGambalCtrlInfo.cameraTxCnt = 0U;
  DRIVER_STEPPER_Init();
}

void FUNC_GAMBAL_SendCameraAngle(void) {
  uint8_t data[VERISON_DATA_LEN];
  float yaw_deg = __GAMBAL_GetFbdAxisAngle(STEPPER_ADDR_YAW, GAMBAL_YAW_DIR);
  float pitch_deg = __GAMBAL_GetFbdAxisAngle(STEPPER_ADDR_PITCH, GAMBAL_PITCH_DIR);

  yaw_deg = __GAMBAL_Limit(yaw_deg, GAMBAL_YAW_MIN_DEG, GAMBAL_YAW_MAX_DEG);
  pitch_deg = __GAMBAL_Limit(pitch_deg, GAMBAL_PITCH_MIN_DEG, GAMBAL_PITCH_MAX_DEG);

  memset(data, 0, sizeof(data));
  /* Keep the same float order as camera->STM32 frames: yaw first, pitch second. */
  __GAMBAL_WriteFloatLE(&data[0], yaw_deg);
  __GAMBAL_WriteFloatLE(&data[4], pitch_deg);
  (void)DRIVER_VERISON_SendData(data, sizeof(data));
}

void FUNC_GAMBAL_Updata(float refYaw, float refPitch) {
  float yawLimit = __GAMBAL_Limit(refYaw, GAMBAL_YAW_MIN_DEG, GAMBAL_YAW_MAX_DEG);
  float pitchLimit = __GAMBAL_Limit(refPitch, GAMBAL_PITCH_MIN_DEG, GAMBAL_PITCH_MAX_DEG);

  if(sGambalCtrlInfo.filterReady == 0U) {
    sGambalCtrlInfo.refYaw_deg = yawLimit;
    sGambalCtrlInfo.refPitch_deg = pitchLimit;
    sGambalCtrlInfo.filterReady = 1U;
  } else {
    sGambalCtrlInfo.refYaw_deg = __GAMBAL_LowPass(sGambalCtrlInfo.refYaw_deg, yawLimit);
    sGambalCtrlInfo.refPitch_deg = __GAMBAL_LowPass(sGambalCtrlInfo.refPitch_deg, pitchLimit);
  }

  sGambalCtrlInfo.yawPulse = __GAMBAL_AngleToPulse(sGambalCtrlInfo.refYaw_deg * GAMBAL_YAW_DIR);
  sGambalCtrlInfo.pitchPulse = __GAMBAL_AngleToPulse(sGambalCtrlInfo.refPitch_deg * GAMBAL_PITCH_DIR);

  DRIVER_STEPPER_Update();

  sGambalCtrlInfo.cameraTxCnt++;
  if(sGambalCtrlInfo.cameraTxCnt >= GAMBAL_CAMERA_TX_PERIOD_CNT) {
    sGambalCtrlInfo.cameraTxCnt = 0U;
    FUNC_GAMBAL_SendCameraAngle();
  }
}
