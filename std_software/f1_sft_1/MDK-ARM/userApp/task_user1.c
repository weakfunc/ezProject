#include "task_user1.h"
#include "driver_oled.h"
#include "driver_steer.h"
#include "driver_tb6612.h"
#include "driver_QR.h"
#include "driver_board.h"
#include "driver_verison.h"
#include "driver_GY615.h"
#include "driver_ws2812.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_OLED_Init();
	DRIVER_GY615_Init();
	DRIVER_GY615_Request();

	DRIVER_WS2812_Init();
	DRIVER_WS2812_SetCtrlGpio(GPIO_ID_USER_IO_1);
	DRIVER_WS2812_SetAllColor(100,0,0);
	DRIVER_WS2812_Refresh(16);
}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	
	
			
	
	for(;;){
		user1TaskInfo.user1TaskCnt++;


		
		if(user1TaskInfo.user1TaskCnt % 10 == 0){
			

		}
		
		

		DRIVER_GY615_GetInfo(&GY615Info);

		DRIVER_OLED_ShowString(10,10,"1234");
		DRIVER_OLED_Refresh();

		osDelay(10);
	}
}

