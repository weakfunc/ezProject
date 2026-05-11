/*============================================================================
 * 模块说明
 * 激光振镜二轴云台控制模块。
 *
 * 坐标系约定：
 *   屏幕中心为原点，右为 +X，上为 +Y，单位：度（°）
 *
 * 电机方向（已由硬件实测确认）：
 *   YAW  轴：电机 CW（正值）→ 激光向右（+X），工作中心 = -45°
 *   PITCH轴：电机 CW（正值）→ 激光向上（+Y），工作中心 = -35°
 *
 * 转换公式：
 *   θ_yaw   = -45 + screen_x
 *   θ_pitch = -35 + screen_y
 *
 * 软件限位：screen_x 和 screen_y 均限制在 ±15° 以内
 *
 * 必须调用：
 *   FUNC_GAMBAL_Init()   — 初始化，内部调用 DRIVER_STEPPER_Init()
 *   FUNC_GAMBAL_Updata() — 2ms 周期调用
 *============================================================================*/

#include "func_gambal.h"
#include "driver_stepperMotor.h"

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* 云台 FD 位置模式速度和加速度，均取驱动支持的最大值。 */
#define GAMBAL_POS_VEL_RPM         3000   /* 最大转速 3000 RPM */
#define GAMBAL_POS_ACC             255U   /* 最大加速度档位 255 */

/* 角度到脉冲的转换系数，STEPPER_PULSE_PER_REV 个脉冲对应 360 度。 */
#define GAMBAL_PULSE_PER_DEG       ((float)STEPPER_PULSE_PER_REV / 360.0f)

/* 屏幕中心对应的电机绝对位置（回零后编码器为 0，需运动到此处激光才打到屏幕中心）。 */
#define GAMBAL_YAW_CENTER_DEG      (-45.0f)
#define GAMBAL_PITCH_CENTER_DEG    (-35.0f)

/* GotoXY 坐标到角度的转换系数：GAMBAL_SCREEN_LIMIT_DEG / GAMBAL_XY_RANGE = 15/100 = 0.15 °/unit。 */
#define GAMBAL_XY_TO_DEG           (GAMBAL_SCREEN_LIMIT_DEG / GAMBAL_XY_RANGE)



/*============================================================================
 * 私有类型定义
 *============================================================================*/

typedef struct {
  float   refYaw_deg;   /* 限位后的屏幕 X 参考坐标，单位：度 */
  float   refPitch_deg; /* 限位后的屏幕 Y 参考坐标，单位：度 */
  int32_t yawPulse;     /* 换算后的 yaw 电机绝对目标脉冲 */
  int32_t pitchPulse;   /* 换算后的 pitch 电机绝对目标脉冲 */
  uint8_t posMode;      /* 步进电机 FD 位置模式，默认 STEPPER_MODE_ABS */
} gambalCtrlInfo_t;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 为了便于调试器观察，模块状态不声明为 static。 */
gambalCtrlInfo_t sGambalCtrlInfo;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 将带符号角度转换为带符号电机脉冲，四舍五入。 */
static int32_t __GAMBAL_AngleToPulse(float angle_deg) {
  float pulse = angle_deg * GAMBAL_PULSE_PER_DEG;

  if(pulse >= 0.0f) {
    return (int32_t)(pulse + 0.5f);
  }
  return (int32_t)(pulse - 0.5f);
}

/* 将数值限制在 [min, max] 范围内。 */
static float __GAMBAL_Limit(float value, float min, float max) {
  if(value < min) {
    return min;
  }
  if(value > max) {
    return max;
  }
  return value;
}

/* 根据反馈圈数计算电机编码器角度，单位：度。 */
static float __GAMBAL_GetMotorEncoderAngle_deg(uint8_t motorId) {
  return STEPPER_INFO(motorId).fbdPosRevs * 360.0f;
}

/* 发送一次 FD 绝对位置命令，负脉冲通过速度方向编码。 */
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
 * 弱函数覆盖
 *============================================================================*/

/* Axis0 为电机地址 0x01，对应 pitch 轴。 */
void DRIVER_STEPPER_Axis0CtrlHook(void) {
  __GAMBAL_SetPos(STEPPER_ADDR_PITCH, sGambalCtrlInfo.pitchPulse);
}

/* Axis1 为电机地址 0x02，对应 yaw 轴。 */
void DRIVER_STEPPER_Axis1CtrlHook(void) {
  __GAMBAL_SetPos(STEPPER_ADDR_YAW, sGambalCtrlInfo.yawPulse);
}

void DRIVER_STEPPER_CommonCtrlHook(void) {
  /* 预留通用钩子，暂未使用。 */
}

/*============================================================================
 * 对外接口
 *============================================================================*/

void FUNC_GAMBAL_Init(void) {
  /* 绝对位置模式，以回零后的零点为参考。 */
  sGambalCtrlInfo.posMode = STEPPER_MODE_ABS;

  DRIVER_STEPPER_Init();
}

void FUNC_GAMBAL_GetAngleInfo(gambalAngleInfo_t *info) {
  info->refYaw_deg   = sGambalCtrlInfo.refYaw_deg;
  info->refPitch_deg = sGambalCtrlInfo.refPitch_deg;
  /* 反推屏幕坐标：screen = motor - CENTER，motor=-45°时 fbdYaw=0（屏幕中心）。 */
  info->fbdYaw_deg   = __GAMBAL_GetMotorEncoderAngle_deg(STEPPER_ADDR_YAW)   - GAMBAL_YAW_CENTER_DEG;
  info->fbdPitch_deg = __GAMBAL_GetMotorEncoderAngle_deg(STEPPER_ADDR_PITCH) - GAMBAL_PITCH_CENTER_DEG;
}

/* 主更新函数（2ms 周期调用）。
 * refYaw  ：屏幕 X 坐标参考，右为 +X，单位：度，限幅 ±15°
 * refPitch：屏幕 Y 坐标参考，上为 +Y，单位：度，限幅 ±15°
 * 方向约定（硬件实测）：
 *   YAW  正值 → 电机 CW → 激光向右（+X 第一象限方向）
 *   PITCH正值 → 电机 CW → 激光向上（+Y 第一象限方向）
 * 转换公式：
 *   θ_yaw   = -45 + screen_x
 *   θ_pitch = -35 + screen_y
 */
void FUNC_GAMBAL_Updata(float refYaw, float refPitch) {
  float motorYaw_deg;
  float motorPitch_deg;

  /* 软件限位（±15°），避免越界指令发往电机。 */
  sGambalCtrlInfo.refYaw_deg   = __GAMBAL_Limit(refYaw,   -GAMBAL_SCREEN_LIMIT_DEG, GAMBAL_SCREEN_LIMIT_DEG);
  sGambalCtrlInfo.refPitch_deg = __GAMBAL_Limit(refPitch, -GAMBAL_SCREEN_LIMIT_DEG, GAMBAL_SCREEN_LIMIT_DEG);

  /* 坐标转换为电机绝对目标角度：
   *   θ_yaw   = -45 + screen_x  （screen_x>0 电机 CW，激光向右）
   *   θ_pitch = -35 + screen_y  （screen_y>0 电机 CW，激光向上）
   */
  motorYaw_deg   = GAMBAL_YAW_CENTER_DEG   + sGambalCtrlInfo.refYaw_deg;
  motorPitch_deg = GAMBAL_PITCH_CENTER_DEG + sGambalCtrlInfo.refPitch_deg;

  sGambalCtrlInfo.yawPulse   = __GAMBAL_AngleToPulse(motorYaw_deg);
  sGambalCtrlInfo.pitchPulse = __GAMBAL_AngleToPulse(motorPitch_deg);

  /* 仅控制状态机，1ms 周期调用，双轴切换周期 2ms，无反馈读取。 */
  DRIVER_STEPPER_UpdateCtrlOnly();
}

void FUNC_GAMBAL_GotoXY(float x, float y) {
  /* x/y ∈ [-100, +100] → 角度 ∈ [-15°, +15°]，系数 0.15 °/unit。 */
  FUNC_GAMBAL_Updata(x * GAMBAL_XY_TO_DEG, y * GAMBAL_XY_TO_DEG);
}
