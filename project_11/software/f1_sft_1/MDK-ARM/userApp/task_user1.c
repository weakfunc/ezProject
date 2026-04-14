#include "task_user1.h"
#include "func_appcom.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_ble.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_BLE_Init();
	DRIVER_OLED_Init();

	STDLIB_TIM_PwmSetDuty(PWM_TIM2_CH1, 100);
}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;
		
		if(user1TaskInfo.user1TaskCnt % 5 == 0){
			DRIVER_OLED_Clear();



			DRIVER_OLED_Refresh();

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
