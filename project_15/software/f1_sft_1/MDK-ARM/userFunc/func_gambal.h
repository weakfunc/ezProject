#ifndef __FUNC_GAMBAL_H__
#define __FUNC_GAMBAL_H__

#include <stdint.h>

/*============================================================================
 * 向上提供（func层向task层提供）
 *============================================================================*/

/* 出射点到屏幕的物理距离，单位：mm。 */
#define GAMBAL_SCREEN_DIST_MM      150.0f

/* 屏幕坐标软件限位，单位：度。screen_x/screen_y 均限制在 ±GAMBAL_SCREEN_LIMIT_DEG 内。 */
#define GAMBAL_SCREEN_LIMIT_DEG    15.0f

/* GotoXY API 坐标量程。x/y ∈ [-GAMBAL_XY_RANGE, +GAMBAL_XY_RANGE] 对应 ±GAMBAL_SCREEN_LIMIT_DEG。 */
#define GAMBAL_XY_RANGE            100.0f

/* 云台角度信息，供任务层查询显示。 */
typedef struct {
  float refYaw_deg;    /* 滤波后的屏幕参考 X 坐标，单位：度 */
  float refPitch_deg;  /* 滤波后的屏幕参考 Y 坐标，单位：度 */
  float fbdYaw_deg;    /* yaw 轴反馈换算回屏幕 X 坐标，单位：度 */
  float fbdPitch_deg;  /* pitch 轴反馈换算回屏幕 Y 坐标，单位：度 */
} gambalAngleInfo_t;

/* 初始化云台控制模块，内部调用 DRIVER_STEPPER_Init()。 */
void FUNC_GAMBAL_Init(void);

/* 主更新函数（每 2ms 调用）。
 * refYaw  ：屏幕 X 坐标参考，单位：度，右为正
 * refPitch：屏幕 Y 坐标参考，单位：度，上为正
 */
void FUNC_GAMBAL_Updata(float refYaw, float refPitch);

/* 获取当前云台参考/反馈角度信息快照。 */
void FUNC_GAMBAL_GetAngleInfo(gambalAngleInfo_t *info);

/* 光斑屏幕坐标控制（每 2ms 调用）。
 * x：屏幕 X 坐标，右为正，范围 [-100, +100]，对应 YAW  ±15°
 * y：屏幕 Y 坐标，上为正，范围 [-100, +100]，对应 PITCH ±15°
 * 映射关系（以 yaw 为例，pitch 同）：
 *   物理式：x_mm = L × tan(θ × π/180)，L = GAMBAL_SCREEN_DIST_MM = 150mm
 *   ±15° 对应屏幕物理范围 ±150×tan(15°) ≈ ±40.2mm
 *   API 坐标 ±100 线性映射到 ±15°：θ = x × (15/100)
 */
void FUNC_GAMBAL_GotoXY(float x, float y);

#endif  /* __FUNC_GAMBAL_H__ */
