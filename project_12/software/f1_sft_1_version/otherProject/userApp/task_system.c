#include "task_system.h"
#include "driver_board.h"

systemTaskInfo_t systemTaskInfo;


uint8_t key1;
uint8_t remoteControl;


void systemTaskInit(){
	STDLIB_COMMON_PeriphInit();
	STDLIB_TIM_PwmInit();
	STDLIB_USART_Init();
	STDLIB_I2C_Init();
	
}

void systemTaskUpdata(void *argument){
	systemTaskInit();
	
	for(;;){
		systemTaskInfo.systemTaskCnt++;
		STDLIB_USART_Updata();
		
		if(DRIVER_BOARD_KeyIsPressed(BOARD_KEY1) || remoteControl){
			key1 = 1;
		}
//		if(!remoteControl){
//			key1 = 0;
//		}
		
		if(systemTaskInfo.systemTaskCnt % 100 == 0){
			
		}
		
    osDelay(100);
  }
}
