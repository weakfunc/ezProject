#include "task_system.h"
#include "func_appcom.h"
#include "driver_ble.h"

systemTaskInfo_t systemTaskInfo;

void systemTaskInit(){
	STDLIB_DWT_Init();
	STDLIB_COMMON_PeriphInit();
	STDLIB_TIM_PwmInit();
	STDLIB_I2C_Init();
	STDLIB_USART_Init();
	FUNC_APPCOM_Init();
	DRIVER_BLE_Init();
}

void systemTaskUpdata(void *argument){
	systemTaskInit();
	
  for(;;){
		systemTaskInfo.systemTaskCnt++;
		
		if(systemTaskInfo.systemTaskCnt % 10 == 0){
			STDLIB_USART_Updata();
			FUNC_APPCOM_TxUpdate();
			FUNC_APPCOM_RxUpdate();
		}
		
		if(systemTaskInfo.systemTaskCnt % 100 == 0){
			
		}
		
    osDelay(10);
  }
}
