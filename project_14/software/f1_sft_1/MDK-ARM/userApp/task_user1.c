#include "task_user1.h"
#include "func_appcom.h"
#include "func_func.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_ble.h"
#include "driver_mpu6050.h"
#include "driver_steer.h"
user1TaskInfo_t user1TaskInfo;

float yawRef;
float pitchRef;

void user1TaskInit(){
	DRIVER_BLE_Init();
	/* 等待OLED VCC上电稳定（SSD1306要求VCC稳定后≥100ms才可接受I2C命令） */
	osDelay(100);
	DRIVER_OLED_Init();
//	DRIVER_STEER_Rotate180(STEER_SERVO_1, 500); 
//	DRIVER_STEER_Rotate180(STEER_SERVO_2, 500); 
	Gimbal_Init();
	Gimbal_SetTarget(yawRef, pitchRef);
}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;

		if(user1TaskInfo.user1TaskCnt % 5 == 0){
			/* 10ms TASK */
			Gimbal_SetTarget(yawRef, pitchRef);
			Gimbal_Task();
		}

		if(user1TaskInfo.user1TaskCnt % 25 == 0){
			/* 50ms TASK */

		}

		if(user1TaskInfo.user1TaskCnt % 50 == 0){
			/* 100ms TASK */

		}

		if(user1TaskInfo.user1TaskCnt % 100 == 0){
			/* 200ms TASK：刷新OLED，显示期望/反馈的yaw和pitch角度 */
			DRIVER_OLED_Clear();
			DRIVER_OLED_ShowString(0,  0, "Y tgt:");
			DRIVER_OLED_ShowFloat( 36, 0, funcInfo.yawTarget, 1);
			DRIVER_OLED_ShowString(0,  8, "Y fdb:");
			DRIVER_OLED_ShowFloat( 36, 8, mpu6050Info.angle.yawDeg, 1);
			DRIVER_OLED_ShowString(0, 16, "P tgt:");
			DRIVER_OLED_ShowFloat( 36, 16, funcInfo.pitchTarget, 1);
			DRIVER_OLED_ShowString(0, 24, "P fdb:");
			DRIVER_OLED_ShowFloat( 36, 24, mpu6050Info.angle.pitchDeg, 1);
			DRIVER_OLED_Refresh();
		}

		if(user1TaskInfo.user1TaskCnt % 500 == 0){
			/* 1000ms TASK */

		}

		osDelay(2);
	}
}
