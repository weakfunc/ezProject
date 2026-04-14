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

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_BLE_Init();
	DRIVER_OLED_Init();
	DRIVER_GY615_Init();
	DRIVER_GY615_Request();
	
}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;
		
		if(user1TaskInfo.user1TaskCnt % 5 == 0){
			/* 10ms TASK*/
			uint8_t xPos;

			DRIVER_OLED_Clear();

			/* 显示标题 */
			DRIVER_OLED_ShowString(0, 0, "GY615 Temp:");

			/* 显示目标温度 TO */
			DRIVER_OLED_ShowString(0, 10, "TO:");
			xPos = DRIVER_OLED_ShowFloat(18, 10, GY615Info.targetTempC, 1);
			DRIVER_OLED_ShowString(xPos, 10, "C");

			/* 显示环境温度 TA */
			DRIVER_OLED_ShowString(0, 20, "TA:");
			xPos = DRIVER_OLED_ShowFloat(18, 20, GY615Info.ambientTempC, 1);
			DRIVER_OLED_ShowString(xPos, 20, "C");

			/* 显示体温 BO */
			DRIVER_OLED_ShowString(0, 30, "BO:");
			xPos = DRIVER_OLED_ShowFloat(18, 30, GY615Info.bodyTempC, 1);
			DRIVER_OLED_ShowString(xPos, 30, "C");

			DRIVER_OLED_Refresh();

		}
		
		if(user1TaskInfo.user1TaskCnt % 25 == 0){
			/* 50ms TASK*/
			DRIVER_GY615_Request();
			DRIVER_GY615_GetInfo(&GY615Info);
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
