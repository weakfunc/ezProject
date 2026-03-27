#include "task_user1.h"
#include "driver_oled.h"
#include "driver_steer.h"
#include "driver_tb6612.h"
#include "driver_QR.h"
#include "driver_board.h"
#include "driver_verison.h"
#include "driver_GY615.h"
#include "driver_ws2812.h"
#include "driver_mp3.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_WS2812_Init();
	DRIVER_WS2812_Clear(WS2812_CH_0);
	DRIVER_WS2812_Clear(WS2812_CH_1);
	DRIVER_WS2812_Refresh(WS2812_CH_0, 16);
	DRIVER_WS2812_Refresh(WS2812_CH_1, 16);
	
	DRIVER_MP3_Init();
	DRIVER_MP3_Play();
}

void user1TaskUpdata(void *argument){
	user1TaskInit();	
	for(;;){
		user1TaskInfo.user1TaskCnt++;
		
		if(user1TaskInfo.user1TaskCnt % 50 == 0){
				
		}

		osDelay(10);
	}
}

