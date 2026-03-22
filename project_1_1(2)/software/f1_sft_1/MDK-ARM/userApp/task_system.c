#include "task_system.h"
#include "driver_board.h"

systemTaskInfo_t systemTaskInfo;
usartInfo_t usartInfo[3];

uint8_t key1;

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
		
		if(DRIVER_BOARD_KeyIsPressed(BOARD_KEY1)){
			key1 = 1;
		
		}
		
		if(systemTaskInfo.systemTaskCnt % 100 == 0){
			
		}
		
    osDelay(10);
  }
}
