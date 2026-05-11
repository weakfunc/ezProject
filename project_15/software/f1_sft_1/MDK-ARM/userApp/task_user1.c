#include "task_user1.h"
#include "func_appcom.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_ble.h"
#include "func_gambal.h"

user1TaskInfo_t user1TaskInfo;

static void __USER1_OledShowAngle(uint8_t y, const char *name, float value) {
	DRIVER_OLED_ShowString(0, y, name);
	(void)DRIVER_OLED_ShowFloat(42, y, value, 1U);
}

static void __USER1_OledUpdata(void) {
	gambalAngleInfo_t angleInfo;

	FUNC_GAMBAL_GetAngleInfo(&angleInfo);

	DRIVER_OLED_Clear();

	/* 现有 OLED 字库仅支持 ASCII，这里使用英文缩写显示连接和角度。 */


	__USER1_OledShowAngle(12, "REF_P:", angleInfo.refPitch_deg);
	__USER1_OledShowAngle(24, "REF_Y:", angleInfo.refYaw_deg);
	__USER1_OledShowAngle(36, "FBD_P:", angleInfo.fbdPitch_deg);
	__USER1_OledShowAngle(48, "FBD_Y:", angleInfo.fbdYaw_deg);

	DRIVER_OLED_Refresh();
}

void user1TaskInit(){
	DRIVER_BLE_Init();
	/* 等待OLED VCC上电稳定（SSD1306要求VCC稳定后≥100ms才可接受I2C命令） */
	osDelay(100);
	
	DRIVER_OLED_Init();
	FUNC_GAMBAL_Init();
}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;

		FUNC_GAMBAL_GotoXY(user1TaskInfo.xRef, user1TaskInfo.yRef);

		if(user1TaskInfo.user1TaskCnt % 5 == 0){
			/* 10ms TASK*/

		}

		if(user1TaskInfo.user1TaskCnt % 25 == 0){
			/* 50ms TASK*/

		}

		if(user1TaskInfo.user1TaskCnt % 50 == 0){
			/* 100ms TASK*/
			__USER1_OledUpdata();
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
