#ifndef __DRIVER_STEER_H__
#define __DRIVER_STEER_H__

#include <stdint.h>
#include "stdlib_tim.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 * 依赖：stdlib_tim、stdlib_dwt（stdlib_dwt仅在.c中使用）
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

/* 单路舵机非阻塞指令队列容量 */
#define STEER_CMD_QUEUE_LEN                  (4U)

/* 舵机模块信息结构体（每路一个元素） */
typedef struct {
  uint8_t  isRunning; /* 当前是否在旋转中 */
  uint8_t  dir;       /* 当前旋转方向 */
  uint32_t timeMs;    /* 当前指令旋转时长（ms） */
  uint8_t  queueCnt;  /* 队列中待执行的指令数（不含当前正在执行的） */
} steerInfo_t;

extern steerInfo_t steerInfo[STEER_SERVO_COUNT];

/* 180度舵机角度控制：steerId取值为STEER_SERVO_x，anglePulse取值为STEER_ANGLE_x */
void DRIVER_STEER_Rotate180(uint8_t steerId, uint16_t anglePulse);

/* 360度舵机非阻塞旋转：将指令入队，立即返回；timeMs=0时立即停止并清空队列 */
void DRIVER_STEER_Rotate360(uint8_t steerId, uint8_t dir, uint32_t timeMs);

/* 非阻塞旋转状态更新，需在task层每2ms周期调用 */
void DRIVER_STEER_Updata(void);

void DRIVER_STEER_DebugMode(void);

#endif
