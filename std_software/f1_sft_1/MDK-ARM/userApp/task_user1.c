#include "task_user1.h"
#include "driver_oled.h"
#include "driver_steer.h"
#include "driver_tb6612.h"
#include "driver_QR.h"
#include "driver_board.h"
#include "driver_verison.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_OLED_Init();

	DRIVER_BOARD_KeyInit();
	DRIVER_VERISON_Init();
}

uint32_t QR;

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;

		DRIVER_STEER_DebugMode();
		DRIVER_TB6612_DebugMode();

		DRIVER_BOARD_KeyInfoUpdate();


		
		DRIVER_VERISON_GetMaixCamInfo();

		DRIVER_OLED_ShowString(10,10,"1234");
		DRIVER_OLED_Refresh();

		osDelay(10);
	}
}

