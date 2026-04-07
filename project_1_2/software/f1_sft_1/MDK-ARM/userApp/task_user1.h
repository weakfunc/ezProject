#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"
#include "driver_oled.h"
#include "driver_steer.h"
#include "driver_tb6612.h"
#include "driver_board.h"
#include "driver_verison.h"
#include "driver_ble.h"
#include "func_appcom.h"

/* 包裹列表最大容量 */
#define PACK_MAX_NUM         (20U)

/* 从识别到舵机触发的延迟计数（单位：循环次数，循环周期10ms）
 * 目的地A：800×10ms=8s，目的地B：1500×10ms=15s */
#define CONVEYOR_DELAY_A     (user1TaskInfo.timeA)
#define CONVEYOR_DELAY_B     (user1TaskInfo.timeB)

/* 舵机无效编号（目的地C不触发舵机） */
#define SERVO_ID_NONE        (0xFFU)

/* 电机速度范围 */
#define MOTOR_SPEED_RAMP_STEP (1)   /* 每次循环步进量，约1s内从0升至最大值 */

typedef struct {
  uint8_t  aim;          /* 目的地：0x01=A 0x02=B 0x03=C */
  uint32_t outportTime;  /* 舵机触发时刻（taskCnt单位） */
  uint8_t  servoId;      /* 触发的舵机编号，SERVO_ID_NONE=不触发 */
  uint8_t  isOutFlag;    /* 是否已完成分拣 */
	uint8_t packNum;
} packInfo_t;

typedef struct user1TaskInfo{
	uint32_t user1TaskCnt;
	
	uint8_t  packNum;                   		 /* 当前包裹总数（循环使用） */
  uint8_t  servoEnable[STEER_SERVO_COUNT]; /* 舵机触发使能标志 */
	int16_t  motorSpeed;										 /* 电机当前速度，用于软启动斜坡控制 */
	
	uint32_t timeA;			//到达A舵机时间
	uint32_t timeB;			//到达B舵机时间
	
	packInfo_t packList[PACK_MAX_NUM];
}user1TaskInfo_t;

extern void user1TaskInit(void);
extern void user1TaskUpdata(void *argument);

#endif

