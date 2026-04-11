#include "task_system.h"
#include "driver_board.h"
#include "driver_ble.h"
#include "func_appcom.h"
#include "stdlib_flash.h"
#include "task_user1.h"


systemTaskInfo_t systemTaskInfo;

void systemTaskInit(){
	STDLIB_DWT_Init();
	STDLIB_COMMON_PeriphInit();
	STDLIB_TIM_PwmInit();
	STDLIB_I2C_Init();
	STDLIB_USART_Init();

	DRIVER_BOARD_Init();
	FUNC_APPCOM_Init();

	/* 上电读 Flash，恢复 user1TaskInfo；魔术字无效则写入默认值（全零）。
	 * 注意：Flash 恢复后须将 remoteVar_RX[1]（APP 定时分钟）预置为存档值，
	 * 否则 remoteVar 初始为 0，user1HandleAppCommand 首次执行时会命中
	 * appTimer==0 分支，将 Flash 恢复的定时字段全部清零。 */
	STDLIB_FLASH_Init();
	if(STDLIB_FLASH_IsValid() != 0U){
		user1TaskInfo.taskCnt           = flashStore.taskCnt;
		user1TaskInfo.currentTemp       = flashStore.currentTemp;
		user1TaskInfo.targetTemp        = flashStore.targetTemp;
		user1TaskInfo.pidIntegral       = flashStore.pidIntegral;
		user1TaskInfo.pidLastError      = flashStore.pidLastError;
		user1TaskInfo.heaterOn          = flashStore.heaterOn;
		user1TaskInfo.overTempFlag      = flashStore.overTempFlag;
		user1TaskInfo.dryBurnFlag       = flashStore.dryBurnFlag;
		user1TaskInfo.massageMode       = flashStore.massageMode;
		user1TaskInfo.massageModeTarget = flashStore.massageModeTarget;
		user1TaskInfo.motorPwmDuty      = flashStore.motorPwmDuty;
		user1TaskInfo.timerSetSec       = flashStore.timerSetSec;
		user1TaskInfo.timerRemainSec    = flashStore.timerRemainSec;
		user1TaskInfo.timerActive       = flashStore.timerActive;
		user1TaskInfo.sysRunning        = flashStore.sysRunning;
		user1TaskInfo.faultCode         = flashStore.faultCode;

		/* 预置 APP 下行变量为 flash 恢复值，防止 APP 重连时发来的初始默认值覆盖已恢复的参数。
		 * RX[0]: 目标温度（整型℃）  RX[1]: 定时分钟  RX[2]: 按摩模式  RX[3]: 电源状态 */
		remoteInfo.remoteVar_RX[0].var_uint32 = (uint32_t)flashStore.targetTemp;
		remoteInfo.remoteVar_RX[1].var_uint32 = flashStore.timerSetSec / 60U;
		remoteInfo.remoteVar_RX[2].var_uint32 = (uint32_t)flashStore.massageModeTarget;
		remoteInfo.remoteVar_RX[3].var_uint32 = (uint32_t)flashStore.sysRunning;
	} else {
		user1TaskInfo.taskCnt           = 0U;
		user1TaskInfo.currentTemp       = 0.0f;
		user1TaskInfo.targetTemp        = USER1_TARGET_TEMP_DEFAULT;
		user1TaskInfo.pidIntegral       = 0.0f;
		user1TaskInfo.pidLastError      = 0.0f;
		user1TaskInfo.heaterOn          = 0U;
		user1TaskInfo.overTempFlag      = 0U;
		user1TaskInfo.dryBurnFlag       = 0U;
		user1TaskInfo.massageMode       = USER1_MASSAGE_MODE_STOP;
		user1TaskInfo.massageModeTarget = USER1_MASSAGE_MODE_STOP;
		user1TaskInfo.motorPwmDuty      = 0U;
		user1TaskInfo.timerSetSec       = 0U;
		user1TaskInfo.timerRemainSec    = 0U;
		user1TaskInfo.timerActive       = 0U;
		user1TaskInfo.sysRunning        = 0U;
		user1TaskInfo.faultCode         = 0U;

		/* Init 写入 Flash 的是全零，此处将有意义的默认值同步回 flashStore 并立即保存，
		 * 防止 5 秒周期 Save 前断电导致下次上电读到全零。 */
		flashStore.taskCnt           = user1TaskInfo.taskCnt;
		flashStore.currentTemp       = user1TaskInfo.currentTemp;
		flashStore.targetTemp        = user1TaskInfo.targetTemp;
		flashStore.pidIntegral       = user1TaskInfo.pidIntegral;
		flashStore.pidLastError      = user1TaskInfo.pidLastError;
		flashStore.heaterOn          = user1TaskInfo.heaterOn;
		flashStore.overTempFlag      = user1TaskInfo.overTempFlag;
		flashStore.dryBurnFlag       = user1TaskInfo.dryBurnFlag;
		flashStore.massageMode       = user1TaskInfo.massageMode;
		flashStore.massageModeTarget = user1TaskInfo.massageModeTarget;
		flashStore.motorPwmDuty      = user1TaskInfo.motorPwmDuty;
		flashStore.timerSetSec       = user1TaskInfo.timerSetSec;
		flashStore.timerRemainSec    = user1TaskInfo.timerRemainSec;
		flashStore.timerActive       = user1TaskInfo.timerActive;
		flashStore.sysRunning        = user1TaskInfo.sysRunning;
		flashStore.faultCode         = user1TaskInfo.faultCode;
		flashStore.version++;
		STDLIB_FLASH_Save();
	}
}

void systemTaskUpdata(void *argument){
	systemTaskInit();
	
  for(;;){
		systemTaskInfo.systemTaskCnt++;
		STDLIB_USART_Updata();



		if(systemTaskInfo.systemTaskCnt % 10 == 0){
			FUNC_APPCOM_UPDATA();
		}
		
		if(systemTaskInfo.systemTaskCnt % 50 == 0){

		}

		/* 将 user1TaskInfo 全量同步到 flashStore 后写入 Flash。 */
		if(systemTaskInfo.systemTaskCnt % 100 == 0){
			flashStore.taskCnt           = user1TaskInfo.taskCnt;
			flashStore.currentTemp       = user1TaskInfo.currentTemp;
			flashStore.targetTemp        = user1TaskInfo.targetTemp;
			flashStore.pidIntegral       = user1TaskInfo.pidIntegral;
			flashStore.pidLastError      = user1TaskInfo.pidLastError;
			flashStore.heaterOn          = user1TaskInfo.heaterOn;
			flashStore.overTempFlag      = user1TaskInfo.overTempFlag;
			flashStore.dryBurnFlag       = user1TaskInfo.dryBurnFlag;
			flashStore.massageMode       = user1TaskInfo.massageMode;
			flashStore.massageModeTarget = user1TaskInfo.massageModeTarget;
			flashStore.motorPwmDuty      = user1TaskInfo.motorPwmDuty;
			flashStore.timerSetSec       = user1TaskInfo.timerSetSec;
			flashStore.timerRemainSec    = user1TaskInfo.timerRemainSec;
			flashStore.timerActive       = user1TaskInfo.timerActive;
			flashStore.sysRunning        = user1TaskInfo.sysRunning;
			flashStore.faultCode         = user1TaskInfo.faultCode;
			flashStore.version++;
			STDLIB_FLASH_Save();
		}

		if(boardInfo.key[0].pressCount % 2 == 0){
			systemTaskInfo.systemEnable_board = 1;
		}else{
			systemTaskInfo.systemEnable_board = 0;
		}

		if(remoteInfo.systemEnable){
			systemTaskInfo.systemEnable_app = 1;
		}else{
			systemTaskInfo.systemEnable_app = 0;
		}

    osDelay(10);
  }
}
