#include "task_user1.h"
#include "func_appcom.h"
#include "func_func.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_ble.h"
#include "driver_stepperMotor.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_BLE_Init();
	/* 等待OLED VCC上电稳定（SSD1306要求VCC稳定后≥100ms才可接受I2C命令） */
	osDelay(100);
	DRIVER_OLED_Init();
	
    DRIVER_STEPPER_Init();
    osDelay(50);
    DRIVER_STEPPER_Enable(0, 1U, STEPPER_SYNC_NOW);
    osDelay(100);
    /* 将上电位置设为坐标零点，再运动到绝对位置 90°（800脉冲，3200脉冲=1圈）*/
    DRIVER_STEPPER_ZeroPos(0);
    osDelay(50);
    DRIVER_STEPPER_SetPos(0, STEPPER_DIR_CW, 200U, 50U, 800U,
                          STEPPER_MODE_ABS, STEPPER_SYNC_NOW);
}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;

		if(user1TaskInfo.user1TaskCnt % 5 == 0){
			/* 10ms TASK */

		}

		if(user1TaskInfo.user1TaskCnt % 25 == 0){
			/* 50ms TASK */
			DRIVER_STEPPER_ReadPos(0);
			DRIVER_STEPPER_ReadStatus(0);
		}

		if(user1TaskInfo.user1TaskCnt % 50 == 0){
			/* 100ms TASK */

		}

		if(user1TaskInfo.user1TaskCnt % 100 == 0){
			/* 200ms TASK */

		}

		if(user1TaskInfo.user1TaskCnt % 500 == 0){
			/* 1000ms TASK */
			
		}

		osDelay(2);
	}
}
