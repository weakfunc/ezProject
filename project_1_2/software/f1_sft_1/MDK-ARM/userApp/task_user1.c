#include "task_user1.h"
#include "task_system.h"

user1TaskInfo_t user1TaskInfo = {
	.timeA = 1800,
	.timeB = 1800 + 3500 ,
};

/* 检测K210新识别结果，将包裹加入列表并计算触发时刻
 * 移植自uartRevPack()，以k210RxFlag替代QRcodePack.revFlag
 * 每收到一帧驱动层置k210RxFlag=1，此处读取后立即清零，连续相同目的地均可记录 */
static void packRecognize(void){
	uint8_t *pHasNew = DRIVER_VERISON_HasNewData();
	
	if (pHasNew == NULL || *pHasNew == 0U) {
			return;
	}
  *pHasNew = 0U;

  user1TaskInfo.packList[user1TaskInfo.packNum].aim       = verisonInfo.realData.var_4b_1;
  user1TaskInfo.packList[user1TaskInfo.packNum].isOutFlag = 0U;

  switch(verisonInfo.realData.var_4b_1){
    case 0x01:  /* 目的地A：延迟8s后触发舵机1 */
      user1TaskInfo.packList[user1TaskInfo.packNum].outportTime = user1TaskInfo.user1TaskCnt + CONVEYOR_DELAY_A;
      user1TaskInfo.packList[user1TaskInfo.packNum].servoId     = STEER_SERVO_1;
      break;
    case 0x02:  /* 目的地B：延迟15s后触发舵机2 */
      user1TaskInfo.packList[user1TaskInfo.packNum].outportTime = user1TaskInfo.user1TaskCnt + CONVEYOR_DELAY_B;
      user1TaskInfo.packList[user1TaskInfo.packNum].servoId     = STEER_SERVO_2;
      break;
    default:    /* 目的地C：不触发舵机，直接通过 */
      user1TaskInfo.packList[user1TaskInfo.packNum].outportTime = 0U;
      user1TaskInfo.packList[user1TaskInfo.packNum].servoId     = SERVO_ID_NONE;
      break;
  }

  user1TaskInfo.packNum++;
  if(user1TaskInfo.packNum >= PACK_MAX_NUM){
    user1TaskInfo.packNum = 0U;
  }
	
	remoteInfo.remoteVar_TX[4].var_uint32 = user1TaskInfo.packNum;
	remoteInfo.remoteVar_TX[5].var_uint32 = user1TaskInfo.packList[user1TaskInfo.packNum].isOutFlag;
	remoteInfo.remoteVar_TX[6].var_uint32 = user1TaskInfo.packList[user1TaskInfo.packNum].servoId;
  remoteInfo.remoteVar_TX[7].var_uint32 = verisonInfo.realData.var_4b_1;
  remoteInfo.remoteVar_TX[8].var_uint32 = verisonInfo.realData.var_4b_2;
}

/* 遍历包裹列表，到达触发时刻时使能对应舵机
 * 移植自sysControl()，用>=比较避免舵机阻塞期间漏触发 */
static void sysControl(void){
  uint8_t i;
  for(i = 0U; i < user1TaskInfo.packNum; i++){
    if(user1TaskInfo.packList[i].isOutFlag == 0U &&
       user1TaskInfo.packList[i].servoId   != SERVO_ID_NONE &&
       (int32_t)(user1TaskInfo.user1TaskCnt - user1TaskInfo.packList[i].outportTime) >= 0){

      if(user1TaskInfo.packList[i].servoId == STEER_SERVO_1){
        user1TaskInfo.servoEnable[STEER_SERVO_1] = 1U;
      } else if(user1TaskInfo.packList[i].servoId == STEER_SERVO_2){
        user1TaskInfo.servoEnable[STEER_SERVO_2] = 1U;
      }
      user1TaskInfo.packList[i].isOutFlag = 1U;
    }
  }
}

/* 传送带电机控制：KEY1按下时斜坡加速，松开时立即停止
 * 移植自motorTaskUpdata()电机部分，使用TB6612_MOTOR_A驱动传送带 */
static void motorControl(void){
  if(systemTaskInfo.key1Press){
    if(user1TaskInfo.motorSpeed < TB6612_SPEED_MAX){
      user1TaskInfo.motorSpeed += MOTOR_SPEED_RAMP_STEP;
    } else {
      user1TaskInfo.motorSpeed = (int16_t)TB6612_SPEED_MAX;
    }
    DRIVER_TB6612_MotorSetSpeed(TB6612_MOTOR_A, user1TaskInfo.motorSpeed);
  } else {
    user1TaskInfo.motorSpeed = 0;
    DRIVER_TB6612_MotorSetSpeed(TB6612_MOTOR_A, 0);
  }
}

/* 舵机控制：仅在KEY1按下（电机运行）时触发，旋转500ms后自动停止
 * 移植自motorTaskUpdata()舵机部分，以Rotate360替代定时器计数 */
static void servoControl(void){
  uint8_t i;
  if(!systemTaskInfo.key1Press){
    return;
  }
  for(i = 0U; i < STEER_SERVO_COUNT; i++){
    if(user1TaskInfo.servoEnable[i] == 1U){
//			if(i == STEER_SERVO_1){
//				DRIVER_STEER_Rotate360(STEER_SERVO_1, STEER_DIR_CW,  400); /* 顺时针旋转500ms（拨开包裹） */
//				DRIVER_STEER_Rotate360(STEER_SERVO_1, STEER_DIR_CCW, 390); /* 逆时针旋转500ms（回到原位） */
//			}else{
//				DRIVER_STEER_Rotate360(STEER_SERVO_2, STEER_DIR_CCW,  400); /* 顺时针旋转500ms（拨开包裹） */
//				DRIVER_STEER_Rotate360(STEER_SERVO_2, STEER_DIR_CW, 380); /* 逆时针旋转500ms（回到原位） */
//			}
			
			if(i == STEER_SERVO_1){
				DRIVER_STEER_Rotate180(STEER_SERVO_1, 2500); 
				STDLIB_DWT_DelayMs(500);
				DRIVER_STEER_Rotate180(STEER_SERVO_1, 500); 
			}else{
				DRIVER_STEER_Rotate180(STEER_SERVO_2, 2500); 
				STDLIB_DWT_DelayMs(500);
				DRIVER_STEER_Rotate180(STEER_SERVO_2, 500); 
			}
			user1TaskInfo.servoEnable[i] = 0U;
    }
  }
}

void oledControl(){
	DRIVER_OLED_Refresh();
	DRIVER_OLED_ShowString(5,5," PACKAGE SORT SYSTEM ");
	DRIVER_OLED_ShowString(5,15,"system time:");
	DRIVER_OLED_ShowNum(80, 15, systemTaskInfo.systemTaskCnt/100, 3);
	DRIVER_OLED_DrawPoint(100,20,1);					//小数点
	DRIVER_OLED_ShowNum(102, 15, systemTaskInfo.systemTaskCnt%10, 1);
	DRIVER_OLED_ShowString(110,15,"s");
	
	DRIVER_OLED_ShowString(5,25,"----Packge Info----");
	
	DRIVER_OLED_ShowString(5,35,"NUM:");
	DRIVER_OLED_ShowNum(35,35, user1TaskInfo.packNum, 1);
	
	DRIVER_OLED_ShowString(5,45,"system enable:");
	DRIVER_OLED_ShowNum(100 ,45, systemTaskInfo.key1Press, 1);
}

uint8_t i;
uint32_t test1;
uint32_t test2;
uint32_t delay = 500;

void user1TaskInit(){
	DRIVER_BLE_Init();
	DRIVER_OLED_Init();
  DRIVER_BOARD_KeyInit();
	DRIVER_VERISON_Init();
//	DRIVER_STEER_Rotate360(STEER_SERVO_1, STEER_DIR_CW, 0U);
//  DRIVER_STEER_Rotate360(STEER_SERVO_2, STEER_DIR_CW, 0U);
	DRIVER_STEER_Rotate180(STEER_SERVO_1, 500); 
	DRIVER_STEER_Rotate180(STEER_SERVO_2, 500); 
}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;
		
		DRIVER_BOARD_KeyInfoUpdate();
		
		remoteInfo.remoteVar_TX[9].var_uint32 = systemTaskInfo.key1Press;
		
		sysControl();
    motorControl();
    servoControl();
		DRIVER_VERISON_GetMaixCamInfo();
		packRecognize();
    if(user1TaskInfo.user1TaskCnt % 50u == 0U){
      oledControl();
    }
		osDelay(2);
	}
}
