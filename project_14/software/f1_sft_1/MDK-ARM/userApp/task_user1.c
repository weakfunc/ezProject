#include "task_user1.h"
#include "func_appcom.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_ble.h"
#include "driver_verison.h"
#include "func_gambal.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_BLE_Init();
	/* 等待OLED VCC上电稳定（SSD1306要求VCC稳定后≥100ms才可接受I2C命令） */
	osDelay(100);
	DRIVER_VERISON_Init();
	DRIVER_OLED_Init();
	FUNC_GAMBAL_Init();
}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;

		FUNC_GAMBAL_Updata(user1TaskInfo.yawRef, user1TaskInfo.picthRef);

		if(user1TaskInfo.user1TaskCnt % 5 == 0){
			/* 10ms TASK*/
			if(DRIVER_VERISON_Updata() != 0U){
				user1TaskInfo.yawRef = verisonInfo.yaw_deg;
				user1TaskInfo.picthRef = verisonInfo.pitch_deg;
			}
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
