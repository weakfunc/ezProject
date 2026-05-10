/*============================================================================
 * 模块说明
 * 二轴云台控制模块。
 *
 * 主要职责：
 * - 对上层输入的 yaw/pitch 参考角做限幅和滤波；
 * - 将滤波后的角度转换为步进电机脉冲；
 * - 覆盖 driver_stepperMotor 的弱函数钩子并下发电机控制命令；
 * - 每 10ms 向摄像头发送当前反馈角度。
 *
 * 任务层每 2ms 调用 FUNC_GAMBAL_Updata()；本模块在步进电机弱函数
 * 钩子中发送具体控制命令。
 *============================================================================*/

#include "func_gambal.h"
#include "driver_stepperMotor.h"
#include "driver_verison.h"
#include <stddef.h>
#include <string.h>

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* 云台两轴 FD 位置模式默认最大速度和加速度。 */
#define GAMBAL_POS_VEL_RPM             3000
#define GAMBAL_POS_ACC                 150U

/* 角度到脉冲的转换系数，STEP_PER_REV 个脉冲对应 360 度。 */
#define GAMBAL_PULSE_PER_DEG           ((float)STEPPER_PULSE_PER_REV / 360.0f)

/* yaw 轴机械/软件控制范围，单位：度。 */
#define GAMBAL_YAW_MIN_DEG             (-120.0f)
#define GAMBAL_YAW_MAX_DEG             120.0f

/* pitch 轴机械/软件控制范围，单位：度。 */
#define GAMBAL_PITCH_MIN_DEG           (-90.0f)
#define GAMBAL_PITCH_MAX_DEG           90.0f

/* 2ms 控制周期下的低通滤波系数，用于处理不连续的摄像头输入。 */
#define GAMBAL_REF_FILTER_ALPHA        0.15f

/* 每 10ms 向摄像头发送一次当前云台角度。 */
#define GAMBAL_CAMERA_TX_PERIOD_CNT    5U

/* 轴向符号约定：
 * - yaw   ：顺时针旋转为正角度，当前电机命令方向需要取反；
 * - pitch ：顺时针旋转为负角度，当前电机命令方向已经正确。
 */
#define GAMBAL_PITCH_DIR               1.0f
#define GAMBAL_YAW_DIR                 (-1.0f)

/*============================================================================
 * 私有类型定义
 *============================================================================*/

typedef struct {
  float   refYaw_deg;     /* 滤波后的 yaw 参考角，单位：度 */
  float   refPitch_deg;   /* 滤波后的 pitch 参考角，单位：度 */
  int32_t yawPulse;       /* 按轴向换算后的 yaw 目标脉冲 */
  int32_t pitchPulse;     /* 按轴向换算后的 pitch 目标脉冲 */
  uint8_t posMode;        /* 步进电机 FD 位置模式，默认 STEPPER_MODE_ABS */
  uint8_t filterReady;    /* 首次参考值写入滤波器前为 0 */
  uint8_t cameraTxCnt;    /* 2ms 分频计数，用于 10ms 摄像头反馈发送 */
} gambalCtrlInfo_t;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 为了便于调试器观察，模块状态不声明为 static。 */
gambalCtrlInfo_t sGambalCtrlInfo;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 将带符号角度转换为带符号电机脉冲，并做四舍五入。 */
static int32_t __GAMBAL_AngleToPulse(float angle_deg) {
  float pulse = angle_deg * GAMBAL_PULSE_PER_DEG;

  if(pulse >= 0.0f) {
    return (int32_t)(pulse + 0.5f);
  }
  return (int32_t)(pulse - 0.5f);
}

/* 将数值限制在 [min, max] 范围内，用于输入参考和反馈发送。 */
static float __GAMBAL_Limit(float value, float min, float max) {
  if(value < min) {
    return min;
  }
  if(value > max) {
    return max;
  }
  return value;
}

/* 一阶低通滤波，用于平滑不连续的摄像头/上层参考输入。 */
static float __GAMBAL_LowPass(float last, float input) {
  return last + (input - last) * GAMBAL_REF_FILTER_ALPHA;
}

/* 将 float 按小端格式写入 10 字节摄像头负载。 */
static void __GAMBAL_WriteFloatLE(uint8_t *data, float value) {
  memcpy(data, &value, sizeof(value));
}

/* 将步进电机反馈圈数转换为对外使用的云台角度符号。 */
static float __GAMBAL_GetFbdAxisAngle(uint8_t motorId, float dir) {
  return STEPPER_INFO(motorId).fbdPosRevs * 360.0f * dir;
}

/* 读取反馈角并按对应轴的控制范围限幅。 */
static float __GAMBAL_GetLimitedFbdAxisAngle(uint8_t motorId, float dir,
                                             float min, float max) {
  return __GAMBAL_Limit(__GAMBAL_GetFbdAxisAngle(motorId, dir), min, max);
}

/* 发送一次 FD 位置命令，负脉冲通过速度方向编码。 */
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

void DRIVER_STEPPER_Axis0CtrlHook(void) {
  /* Axis0 为电机地址 0x01，对应 pitch 轴。 */
  __GAMBAL_SetPos(STEPPER_ADDR_PITCH, sGambalCtrlInfo.pitchPulse);
}

void DRIVER_STEPPER_Axis1CtrlHook(void) {
  /* Axis1 为电机地址 0x02，对应 yaw 轴。 */
  __GAMBAL_SetPos(STEPPER_ADDR_YAW, sGambalCtrlInfo.yawPulse);
}

void DRIVER_STEPPER_CommonCtrlHook(void) {
  /* 预留通用钩子，后续可放置云台公共动作。 */
}

/*============================================================================
 * 对外接口
 *============================================================================*/

void FUNC_GAMBAL_Init(void) {
  /* 云台默认使用绝对位置模式。 */
  sGambalCtrlInfo.posMode = STEPPER_MODE_ABS;
  /* 第一次更新时直接用输入参考值初始化滤波器。 */
  sGambalCtrlInfo.filterReady = 0U;
  /* 2ms 计数器，累计 5 次即为 10ms 摄像头反馈发送周期。 */
  sGambalCtrlInfo.cameraTxCnt = 0U;

  /* 初始化本功能模块依赖的底层步进电机驱动。 */
  DRIVER_STEPPER_Init();
}

void FUNC_GAMBAL_SendCameraAngle(void) {
  uint8_t data[VERISON_DATA_LEN];
  float yaw_deg = __GAMBAL_GetLimitedFbdAxisAngle(STEPPER_ADDR_YAW,
                                                  GAMBAL_YAW_DIR,
                                                  GAMBAL_YAW_MIN_DEG,
                                                  GAMBAL_YAW_MAX_DEG);
  float pitch_deg = __GAMBAL_GetLimitedFbdAxisAngle(STEPPER_ADDR_PITCH,
                                                    GAMBAL_PITCH_DIR,
                                                    GAMBAL_PITCH_MIN_DEG,
                                                    GAMBAL_PITCH_MAX_DEG);

  memset(data, 0, sizeof(data));
  /* 保持与摄像头发往 STM32 的 float 顺序一致：yaw 在前，pitch 在后。 */
  __GAMBAL_WriteFloatLE(&data[0], yaw_deg);
  __GAMBAL_WriteFloatLE(&data[4], pitch_deg);
  (void)DRIVER_VERISON_SendData(data, sizeof(data));
}

void FUNC_GAMBAL_GetAngleInfo(gambalAngleInfo_t *info) {
  if(info == NULL) {
    return;
  }

  info->refYaw_deg = sGambalCtrlInfo.refYaw_deg;
  info->refPitch_deg = sGambalCtrlInfo.refPitch_deg;
  info->fbdYaw_deg = __GAMBAL_GetLimitedFbdAxisAngle(STEPPER_ADDR_YAW,
                                                     GAMBAL_YAW_DIR,
                                                     GAMBAL_YAW_MIN_DEG,
                                                     GAMBAL_YAW_MAX_DEG);
  info->fbdPitch_deg = __GAMBAL_GetLimitedFbdAxisAngle(STEPPER_ADDR_PITCH,
                                                       GAMBAL_PITCH_DIR,
                                                       GAMBAL_PITCH_MIN_DEG,
                                                       GAMBAL_PITCH_MAX_DEG);
}


/* 轴向符号约定：
 * - yaw   ：顺时针旋转为正角度
 * - pitch ：顺时针旋转为负角度
 */
void FUNC_GAMBAL_Updata(float refYaw, float refPitch) {
  /* 输入参考先限幅再滤波，避免异常跳变直接进入控制链路。 */
  float yawLimit = __GAMBAL_Limit(refYaw, GAMBAL_YAW_MIN_DEG, GAMBAL_YAW_MAX_DEG);
  float pitchLimit = __GAMBAL_Limit(refPitch, GAMBAL_PITCH_MIN_DEG, GAMBAL_PITCH_MAX_DEG);

  if(sGambalCtrlInfo.filterReady == 0U) {
    /* 首次使用时直接初始化低通滤波器，避免从 0 缓慢爬升。 */
    sGambalCtrlInfo.refYaw_deg = yawLimit;
    sGambalCtrlInfo.refPitch_deg = pitchLimit;
    sGambalCtrlInfo.filterReady = 1U;
  } else {
    /* 平滑摄像头或上层输入的不连续参考值。 */
    sGambalCtrlInfo.refYaw_deg = __GAMBAL_LowPass(sGambalCtrlInfo.refYaw_deg, yawLimit);
    sGambalCtrlInfo.refPitch_deg = __GAMBAL_LowPass(sGambalCtrlInfo.refPitch_deg, pitchLimit);
  }

  /* 滤波后按轴向符号约定换算，再转换为带符号脉冲。 */
  sGambalCtrlInfo.yawPulse = __GAMBAL_AngleToPulse(sGambalCtrlInfo.refYaw_deg * GAMBAL_YAW_DIR);
  sGambalCtrlInfo.pitchPulse = __GAMBAL_AngleToPulse(sGambalCtrlInfo.refPitch_deg * GAMBAL_PITCH_DIR);

  /* 步进电机更新函数执行 5 步读参/控制状态机。 */
  DRIVER_STEPPER_Update();

  /* 摄像头反馈发送集成在此处，每 10ms 执行一次。 */
  sGambalCtrlInfo.cameraTxCnt++;
  if(sGambalCtrlInfo.cameraTxCnt >= GAMBAL_CAMERA_TX_PERIOD_CNT) {
    sGambalCtrlInfo.cameraTxCnt = 0U;
    FUNC_GAMBAL_SendCameraAngle();
  }
}
