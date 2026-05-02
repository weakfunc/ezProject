#include "task_user1.h"
#include "func_appcom.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_ble.h"

#include "driver_steer.h"
#include "driver_tb6612.h"
#include "driver_QR.h"
#include "driver_verison.h"
#include "driver_GY615.h"
#include "driver_ws2812.h"
#include "driver_mp3.h"
#include "driver_stepperMotor.h"
#include "driver_mpu6050.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_BLE_Init();
	/* 等待OLED VCC上电稳定（SSD1306要求VCC稳定后≥100ms才可接受I2C命令） */
	osDelay(100);
	DRIVER_OLED_Init();
}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;
		
		if(user1TaskInfo.user1TaskCnt % 5 == 0){
			/* 10ms TASK*/

		}
		
		if(user1TaskInfo.user1TaskCnt % 25 == 0){
			/* 50ms TASK*/

		}
		
		if(user1TaskInfo.user1TaskCnt % 50 == 0){
			/* 100ms TASK*/
			
		}
		
		if(user1TaskInfo.user1TaskCnt % 100 == 0){
			/* 200ms TASK*/
		
		}
		
		if(user1TaskInfo.user1TaskCnt % 500 == 0){
			/* 1000ms TASK */

		}

		osDelay(2);
	}
}
