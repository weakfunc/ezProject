#ifndef __FUNC_LEAD_SCREW_H__
#define __FUNC_LEAD_SCREW_H__

#include "driver_stepperMotor.h"

/*============================================================================
 * 配置宏（可按需修改）
 *============================================================================*/

/* 丝杆导程（单位：mm/圈）。顺时针转动（速度>0）为负方向。 */
#define LEADSCRW_PITCH_MM          2.0f

/* 默认电机地址。 */
#define LEADSCRW_MOTOR_ID          0x01U

/* 运行速度（RPM）和加速度档位。 */
#define LEADSCRW_DEFAULT_SPEED_RPM 3000
#define LEADSCRW_DEFAULT_ACC       250U

/*============================================================================
 * 对外数据结构
 *============================================================================*/

/* 丝杆运行状态枚举。 */
typedef enum {
  LEADSCRW_STATE_IDLE    = 0,  /* 静止（等待参数输入） */
  LEADSCRW_STATE_RUNNING = 1,  /* 运行中 */
} leadScrewState_e;

/* 丝杆模块信息结构体。 */
typedef struct {
  leadScrewState_e state;         /* 当前运行状态 */
  float            currentPos_cm; /* 当前相对零点位置（cm，正值为正方向） */
} leadScrewInfo_t;

extern leadScrewInfo_t leadScrewInfo;

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化丝杆模块，清零内部状态。 */
void FUNC_LEADSCREW_Init(void);

/* 丝杆绝对位置控制：移动至相对原点的指定位置。
 * targetCm: 目标位置（cm），正值为正方向，负值为负方向
 * 若当前处于运行中则忽略本次调用
 */
void FUNC_LEADSCREW_SetAbsPos(float targetCm);

/* 丝杆相对距离控制：在当前位置基础上移动指定距离。
 * deltaCm: 移动距离（cm），正值为正方向，负值为负方向
 * 若当前处于运行中则忽略本次调用
 */
void FUNC_LEADSCREW_SetRelPos(float deltaCm);

/* 丝杆状态查询（须周期调用）：检测运动完成，更新 leadScrewInfo。 */
void FUNC_LEADSCREW_Update(void);

/* 丝杆原点标定：将当前丝杆位置定义为零点。 */
void FUNC_LEADSCREW_CalibrateOrigin(void);

#endif /* __FUNC_LEAD_SCREW_H__ */
