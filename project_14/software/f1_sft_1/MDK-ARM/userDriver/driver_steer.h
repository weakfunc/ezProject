#ifndef __DRIVER_STEER_H__
#define __DRIVER_STEER_H__

#include <stdint.h>
#include "stdlib_tim.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 *============================================================================*/
#define STEER_DEP_SERVO_1_PWM_CH             PWM_TIM2_CH1
#define STEER_DEP_SERVO_2_PWM_CH             PWM_TIM2_CH2

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/
#define STEER_SERVO_1                        (0U)
#define STEER_SERVO_2                        (1U)
#define STEER_SERVO_COUNT                    (2U)

#define STEER_ANGLE_0_DEG                    (1000U)
#define STEER_ANGLE_90_DEG                   (2000U)
#define STEER_ANGLE_USER_1                   (1000U)
#define STEER_ANGLE_USER_2                   (1000U)

/* 360度舵机控制参数 */
#define STEER_360_CW                         (1000U)  /* 顺时针旋转脉宽 */
#define STEER_360_CCW                        (2000U)  /* 逆时针旋转脉宽 */
#define STEER_360_STOP                       (1500U)  /* 停止脉宽 */
#define STEER_360_AGNLE_90                   (180)    /* 旋转90度 */

#define STEER_DIR_CW                         (0U)     /* 顺时针 */
#define STEER_DIR_CCW                        (1U)     /* 逆时针 */

/* 舵机模块信息结构体（每路舵机一个元素） */
typedef struct {
  uint8_t  ch;     /* 舵机通道 */
  uint8_t  dir;    /* 旋转方向 */
  uint16_t duty;   /* 脉宽 */
  uint32_t timeMs; /* 旋转时间（ms） */
} steerInfo_t;

extern steerInfo_t steerInfo[STEER_SERVO_COUNT];

/* 180度舵机脉宽控制范围 */
#define STEER_180_PULSE_MIN   (500U)    /* 0°对应脉宽（us） */
#define STEER_180_PULSE_MAX   (2500U)   /* 180°对应脉宽（us） */

/* 180度舵机角度控制：steerId取值为STEER_SERVO_x，anglePulse取值为STEER_ANGLE_x */
void DRIVER_STEER_Rotate180(uint8_t steerId, uint16_t anglePulse);

/* 360度舵机旋转控制：steerId取值为STEER_SERVO_x，dir取值为STEER_DIR_x，timeMs为旋转时间（ms） */
void DRIVER_STEER_Rotate360(uint8_t steerId, uint8_t dir, uint32_t timeMs);

/* 180度舵机角度控制（浮点角度）：steerId取值为STEER_SERVO_x，angleDeg取值范围0.0~180.0 */
void DRIVER_STEER_SetAngleDeg(uint8_t steerId, float angleDeg);

void DRIVER_STEER_DebugMode(void);

#endif
