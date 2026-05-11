#include "task_system.h"
#include "stdlib_flash.h"
#include "stdlib_adc.h"
#include "driver_board.h"
#include "driver_ble.h"
#include "driver_stepperMotor.h"
#include "func_appcom.h"


systemTaskInfo_t systemTaskInfo;

static uint8_t sLastBoardEnable;

void systemTaskInit(){
	STDLIB_DWT_Init();
	STDLIB_COMMON_PeriphInit();
	STDLIB_TIM_PwmInit();
	STDLIB_I2C_Init();
	STDLIB_USART_Init();
	STDLIB_ADC_Init();
	STDLIB_FLASH_Init();
	DRIVER_BOARD_Init();
	FUNC_APPCOM_Init();

}

void systemTaskUpdata(void *argument){
	systemTaskInit();

	

  for(;;){
		systemTaskInfo.systemTaskCnt++;
		STDLIB_USART_Updata();
		DRIVER_BOARD_KeyInfoUpdate();
		DRIVER_BOARD_BuzzUpdate();
		
		if(!systemTaskInfo.systemInitFinshFlag){
			boardInfo.buzzTimeMs = 200;
			systemTaskInfo.systemInitFinshFlag = 1;
		}

		if(systemTaskInfo.systemTaskCnt % 10 == 0){
			FUNC_APPCOM_UPDATA();
		}
		
		if(systemTaskInfo.systemTaskCnt % 50 == 0){
			STDLIB_ADC_Sample();
		}
		
	   if(systemTaskInfo.systemTaskCnt % 100 == 0){

		}
			
		if(systemTaskInfo.systemTaskCnt % 1000 == 0){
			STDLIB_FLASH_Save();
		}

		if(DRIVER_STEPPER_TakeFlashSaveRequest() != 0U){
			flashStore.version++;
			STDLIB_FLASH_Save();
		}

		if(boardInfo.key[1].pressCount != boardInfo.key[1].lastPressCount){
			boardInfo.key[1].lastPressCount = boardInfo.key[1].pressCount;
			DRIVER_STEPPER_RequestSaveAllHomeZero();

		}

		if(boardInfo.key[2].pressCount != boardInfo.key[2].lastPressCount){
			boardInfo.key[2].lastPressCount = boardInfo.key[2].pressCount;

		}
		
		if(boardInfo.key[1].pressCount % 2 == 1){

		}else{

		}
		
		if(boardInfo.key[2].pressCount % 2 == 1){
			
		}else{

		}
		
		if(boardInfo.key[0].pressCount % 2 == 1){
			systemTaskInfo.systemEnable_board = 1;
		}else{
			systemTaskInfo.systemEnable_board = 0;
		}

		if(systemTaskInfo.systemEnable_board != sLastBoardEnable){
			sLastBoardEnable = systemTaskInfo.systemEnable_board;
			DRIVER_STEPPER_RequestBoardEnable(systemTaskInfo.systemEnable_board);
		}
		
		if(remoteInfo.systemEnable){
			systemTaskInfo.systemEnable_app = 1;
		}else{
			systemTaskInfo.systemEnable_app = 0;
		}

    osDelay(10);
  }
}
