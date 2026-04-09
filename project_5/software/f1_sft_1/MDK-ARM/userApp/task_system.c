#include "task_system.h"
#include "driver_board.h"


systemTaskInfo_t systemTaskInfo;

void systemTaskInit(){
	STDLIB_DWT_Init();
	STDLIB_COMMON_PeriphInit();
	STDLIB_TIM_PwmInit();
	STDLIB_I2C_Init();
	STDLIB_USART_Init();

}

void systemTaskUpdata(void *argument){
	systemTaskInit();
	
  for(;;){
		systemTaskInfo.systemTaskCnt++;
		STDLIB_USART_Updata();


		
    osDelay(10);
  }
}
