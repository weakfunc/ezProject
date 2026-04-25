#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"
#include "driver_steer.h"

typedef struct user1TaskInfo{
	uint32_t user1TaskCnt;
}user1TaskInfo_t;

/* 包裹列表最大容量 */
#define PACK_MAX_NUM         (20U)

/* 识别到目标后的传送带延迟计数（单位：user1任务循环次数，每次2ms）
 * 目的地A：timeA*2ms，目的地B：timeB*2ms */
#define CONVEYOR_DELAY_A     (timeA)
#define CONVEYOR_DELAY_B     (timeB)

/* 舵机无效编号（目的地C不触发舵机） */
#define SERVO_ID_NONE        (0xFFU)

/* 电机速度每次循环增量（用于软启动斜坡控制） */
#define MOTOR_SPEED_RAMP_STEP (10)

/* 包裹信息结构体 */
typedef struct {
  uint8_t  aim;          /* 目的地编码（0x31=A 0x32=B 其他=C） */
  uint32_t outportTime;  /* 触发舵机的taskCnt时刻 */
  uint8_t  servoId;      /* 触发的舵机编号，SERVO_ID_NONE=不触发 */
  uint8_t  isOutFlag;    /* 是否已完成分拣 */
  uint8_t  packNum;      /* 包裹序号 */
} packInfo_t;

/* 系统运行状态结构体 */
typedef struct {
  uint32_t taskCnt;                      /* 任务计数器，每次循环+1 */
  uint8_t  packNum;                      /* 当前包裹数量（循环使用） */
  uint8_t  servoEnable[STEER_SERVO_COUNT]; /* 各舵机触发使能标志 */
} sysCtrl_t;

extern void user1TaskInit(void);
extern void user1TaskUpdata(void *argument);

#endif
