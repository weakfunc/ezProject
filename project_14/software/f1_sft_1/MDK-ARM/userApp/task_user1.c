#include "task_user1.h"
#include "func_appcom.h"
#include "func_func.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_ble.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_BLE_Init();
	/* 等待OLED VCC上电稳定（SSD1306要求VCC稳定后≥100ms才可接受I2C命令） */
	osDelay(100);
	DRIVER_OLED_Init();
	/* 初始化云台，使能两轴电机 */
	FUNC_GIMBAL_Init();
	/* 等待电机上电稳定及使能应答 */
	osDelay(200);
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
			/* 轮询云台电机反馈（位置、状态），每50ms执行一次 */
			FUNC_GIMBAL_Update();
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
