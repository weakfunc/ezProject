#include "task_system.h"
#include "driver_board.h"
#include "driver_ble.h"
#include "func_appcom.h"


systemTaskInfo_t systemTaskInfo;

void systemTaskInit(){
	STDLIB_DWT_Init();
	STDLIB_COMMON_PeriphInit();
	STDLIB_TIM_PwmInit();
	STDLIB_I2C_Init();
	STDLIB_USART_Init();
	FUNC_APPCOM_Init();
}

void systemTaskUpdata(void *argument){
	systemTaskInit();
	
  for(;;){
		systemTaskInfo.systemTaskCnt++;
		STDLIB_USART_Updata();

		if(systemTaskInfo.systemTaskCnt % 10 == 0){
//			FUNC_APPCOM_UPDATA();
		}
		
		if(systemTaskInfo.systemTaskCnt % 100 == 0){
			
		}

	   if(DRIVER_BOARD_KeyIsPressed(BOARD_KEY1) || remoteInfo.key1){
			systemTaskInfo.key1Press = 1;
		}else if(!remoteInfo.key1){
			systemTaskInfo.key1Press = 0;
		}

		if(DRIVER_BOARD_KeyIsPressed(BOARD_KEY2) || remoteInfo.key2){
			systemTaskInfo.key2Press = 1;
		}else if(!remoteInfo.key2){
			systemTaskInfo.key2Press = 0;
		}

		if(DRIVER_BOARD_KeyIsPressed(BOARD_KEY3) || remoteInfo.key3){
			systemTaskInfo.key3Press = 1;
		}else if(!remoteInfo.key3){
			systemTaskInfo.key3Press = 0;
		}
		
    osDelay(10);
  }
}
