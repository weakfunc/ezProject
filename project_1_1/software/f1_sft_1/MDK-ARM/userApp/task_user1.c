#include "task_user1.h"
#include "driver_oled.h"
#include "driver_steer.h"
#include "driver_tb6612.h"
#include "driver_board.h"
#include "driver_verison.h"
#include "task_system.h"

/*============================================================================
 * 私有宏定义
 *============================================================================*/


uint32_t timeA = 1400;
uint32_t timeB = 8500;


/* 包裹列表最大容量 */
#define PACK_MAX_NUM         (20U)

/* 从识别到舵机触发的延迟计数（单位：循环次数，循环周期10ms）
 * 目的地A：800×10ms=8s，目的地B：1500×10ms=15s */
#define CONVEYOR_DELAY_A     (timeA)
#define CONVEYOR_DELAY_B     (timeB)

/* 舵机无效编号（目的地C不触发舵机） */
#define SERVO_ID_NONE        (0xFFU)

/* 电机速度范围 */
#define MOTOR_SPEED_RAMP_STEP (10)   /* 每次循环步进量，约1s内从0升至最大值 */

/*============================================================================
 * 私有结构体定义
 *============================================================================*/

/* 包裹信息（移植自QRcodePack_t，仅保留分拣所需字段） */
typedef struct {
  uint8_t  aim;          /* 目的地：0x01=A 0x02=B 0x03=C */
  uint32_t outportTime;  /* 舵机触发时刻（taskCnt单位） */
  uint8_t  servoId;      /* 触发的舵机编号，SERVO_ID_NONE=不触发 */
  uint8_t  isOutFlag;    /* 是否已完成分拣 */
} packInfo_t;

/* 系统运行时状态（移植自sysConfig_t） */
typedef struct {
  uint32_t taskCnt;                   /* 任务计数器，每次循环+1 */
  uint8_t  packNum;                   /* 当前包裹总数（循环使用） */
  uint8_t  servoEnable[STEER_SERVO_COUNT]; /* 舵机触发使能标志 */
} sysCtrl_t;

extern uint8_t key1;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 包裹列表 */
packInfo_t packList[PACK_MAX_NUM];

/* 系统状态 */
sysCtrl_t sysCtrl;

/* 电机当前速度，用于软启动斜坡控制 */
int16_t motorSpeed;

user1TaskInfo_t user1TaskInfo;

uint32_t QR;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 检测K210新识别结果，将包裹加入列表并计算触发时刻
 * 移植自uartRevPack()，以k210RxFlag替代QRcodePack.revFlag
 * 每收到一帧驱动层置k210RxFlag=1，此处读取后立即清零，连续相同目的地均可记录 */
static void packRecognize(void){
	uint8_t *pHasNew = DRIVER_VERISON_HasNewData();
	
	if (pHasNew == NULL || *pHasNew == 0U) {
			return;
	}
  *pHasNew = 0U;

  packList[sysCtrl.packNum].aim       = versionRealData.field.var1;;
  packList[sysCtrl.packNum].isOutFlag = 0U;

  switch(versionRealData.field.var1){
    case 0x31:  /* 目的地A：延迟8s后触发舵机1 */
      packList[sysCtrl.packNum].outportTime = sysCtrl.taskCnt + CONVEYOR_DELAY_A;
      packList[sysCtrl.packNum].servoId     = STEER_SERVO_1;
      break;
    case 0x32:  /* 目的地B：延迟15s后触发舵机2 */
      packList[sysCtrl.packNum].outportTime = sysCtrl.taskCnt + CONVEYOR_DELAY_B;
      packList[sysCtrl.packNum].servoId     = STEER_SERVO_2;
      break;
    default:    /* 目的地C：不触发舵机，直接通过 */
      packList[sysCtrl.packNum].outportTime = 0U;
      packList[sysCtrl.packNum].servoId     = SERVO_ID_NONE;
      break;
  }

  sysCtrl.packNum++;
  if(sysCtrl.packNum >= PACK_MAX_NUM){
    sysCtrl.packNum = 0U;
  }
}

/* 遍历包裹列表，到达触发时刻时使能对应舵机
 * 移植自sysControl()，用>=比较避免舵机阻塞期间漏触发 */
static void sysControl(void){
  uint8_t i;
  for(i = 0U; i < sysCtrl.packNum; i++){
    if(packList[i].isOutFlag == 0U &&
       packList[i].servoId   != SERVO_ID_NONE &&
       (int32_t)(sysCtrl.taskCnt - packList[i].outportTime) >= 0){

      if(packList[i].servoId == STEER_SERVO_1){
        sysCtrl.servoEnable[STEER_SERVO_1] = 1U;
      } else if(packList[i].servoId == STEER_SERVO_2){
        sysCtrl.servoEnable[STEER_SERVO_2] = 1U;
      }
      packList[i].isOutFlag = 1U;
    }
  }
}

/* 传送带电机控制：KEY1按下时斜坡加速，松开时立即停止
 * 移植自motorTaskUpdata()电机部分，使用TB6612_MOTOR_A驱动传送带 */
static void motorControl(void){
  if(key1){
    if(motorSpeed < TB6612_SPEED_MAX){
      motorSpeed += MOTOR_SPEED_RAMP_STEP;
    } else {
      motorSpeed = (int16_t)TB6612_SPEED_MAX;
    }
    DRIVER_TB6612_MotorSetSpeed(TB6612_MOTOR_A, -motorSpeed);
  } else {
    motorSpeed = 0;
    DRIVER_TB6612_MotorSetSpeed(TB6612_MOTOR_A, 0);
  }
}

/* 舵机控制：仅在KEY1按下（电机运行）时触发，旋转500ms后自动停止
 * 移植自motorTaskUpdata()舵机部分，以Rotate360替代定时器计数 */
static void servoControl(void){
  uint8_t i;
  if(!key1){
    return;
  }
  for(i = 0U; i < STEER_SERVO_COUNT; i++){
    if(sysCtrl.servoEnable[i] == 1U){
			if(i == STEER_SERVO_1){
				DRIVER_STEER_Rotate360(STEER_SERVO_1, STEER_DIR_CW,  400); /* 顺时针旋转500ms（拨开包裹） */
				DRIVER_STEER_Rotate360(STEER_SERVO_1, STEER_DIR_CCW, 390); /* 逆时针旋转500ms（回到原位） */
			}else{
				DRIVER_STEER_Rotate360(STEER_SERVO_2, STEER_DIR_CCW,  400); /* 顺时针旋转500ms（拨开包裹） */
				DRIVER_STEER_Rotate360(STEER_SERVO_2, STEER_DIR_CW, 380); /* 逆时针旋转500ms（回到原位） */
			}
			sysCtrl.servoEnable[i] = 0U;
    }
  }
}

void oledControl(){
	DRIVER_OLED_Refresh();
	DRIVER_OLED_ShowString(5,5," PACKAGE SORT SYSTEM ");
	DRIVER_OLED_ShowString(5,15,"system time:");
	DRIVER_OLED_ShowNum(80, 15, systemTaskInfo.systemTaskCnt/10, 3);
	DRIVER_OLED_DrawPoint(100,20,1);					//小数点
	DRIVER_OLED_ShowNum(102, 15, systemTaskInfo.systemTaskCnt%10, 1);
	DRIVER_OLED_ShowString(110,15,"s");
	
	DRIVER_OLED_ShowString(5,25,"----Packge Info----");
//	DRIVER_OLED_ShowNum(100, 35, QRcodePack.id, 1, 24, 1);			//包裹编号
	
	DRIVER_OLED_ShowString(5,35,"NUM:");
	DRIVER_OLED_ShowNum(35,35, maixCamInfo.rxTotalCnt, 1);
	
	DRIVER_OLED_ShowString(5,45,"system enable:");
	DRIVER_OLED_ShowNum(100 ,45, key1, 1);
//	switch(packList.source){
//		case 0x01: OLED_ShowString(50,35,"E",8,1); break;
//		case 0x02: OLED_ShowString(50,35,"F",8,1); break;
//		case 0x03: OLED_ShowString(50,35,"G",8,1); break;
//	}
//	OLED_ShowString(5,45,"aim:",8,1);
//	switch(packList.aim){
//		case 0x01: OLED_ShowString(30,45,"A",8,1); break;
//		case 0x02: OLED_ShowString(30,45,"B",8,1); break;
//		case 0x03: OLED_ShowString(30,45,"C",8,1); break;
//	}

}

/*============================================================================
 * 任务入口
 *============================================================================*/

void user1TaskInit(){
  DRIVER_OLED_Init();
  DRIVER_BOARD_KeyInit();
	DRIVER_VERISON_Init();
  /* 初始化360度舵机为停止状态 */
  DRIVER_STEER_Rotate360(STEER_SERVO_1, STEER_DIR_CW, 0U);
  DRIVER_STEER_Rotate360(STEER_SERVO_2, STEER_DIR_CW, 0U);
}

void user1TaskUpdata(void *argument){
  user1TaskInit();
  for(;;){
    user1TaskInfo.user1TaskCnt++;
    sysCtrl.taskCnt++;

    DRIVER_BOARD_KeyInfoUpdate();
    sysControl();
    motorControl();
    servoControl();
		DRIVER_VERISON_GetMaixCamInfo();
		packRecognize();

    /* OLED每100ms刷新一次，避免高频I2C传输拖慢控制循环 */
    if(sysCtrl.taskCnt % 50u == 0U){
      oledControl();
    }
		

    osDelay(2);
  }
}
