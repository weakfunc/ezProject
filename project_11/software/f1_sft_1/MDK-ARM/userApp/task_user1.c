#include "task_user1.h"
#include "func_func.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_senser.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_SENSER_Init();
	/* 等待OLED VCC上电稳定（SSD1306要求VCC稳定后≥100ms才可接受I2C命令） */
	osDelay(100);
	DRIVER_OLED_Init();
	FUNC_FUNC_Init();
}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;

		/* 每次循环（2ms）：测速状态机、按键扫描（传感器状态由中断更新） */
		FUNC_FUNC_SpeedMeasUpdate();
		FUNC_FUNC_KeyUpdate();

		if(user1TaskInfo.user1TaskCnt % 50 == 0){
			/* 100ms周期：刷新OLED显示
			 * 软件I2C全屏刷新耗时约17ms，频率过高会阻塞2ms基础周期 */
			DRIVER_OLED_Clear();
			FUNC_FUNC_DrawUI();
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
