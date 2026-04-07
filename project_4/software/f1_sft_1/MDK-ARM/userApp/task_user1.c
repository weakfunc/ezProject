#include "task_user1.h"
#include "driver_oled.h"
#include "driver_steer.h"
#include "driver_tb6612.h"
#include "driver_board.h"
#include "driver_ble.h"
#include "func_appcom.h"
#include "driver_senser.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_BLE_Init();
	DRIVER_OLED_Init();
	DRIVER_SENSER_Init();
	DRIVER_SENSER_DS18B20Init();
	DRIVER_SENSER_WaterTempCtrlInit();
}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;

		/* 周期更新水位检测状态 */
		DRIVER_SENSER_WaterLvlUpdate();
		
		if(user1TaskInfo.user1TaskCnt % 10 == 0){
//						/* 测试：根据水位通道1的水位状态控制加热模块1占空比 */
//			if(DRIVER_SENSER_WaterLvlGet(SENSER_WATER_LVL_CH_1)){
//				/* 有水：以默认占空比加热 */
//				DRIVER_SENSER_HeatSetDuty(SENSER_HEAT_CH_1, SENSER_HEAT_DUTY_DEFAULT);
//				DRIVER_SENSER_HeatSetDuty(SENSER_HEAT_CH_2, SENSER_HEAT_DUTY_DEFAULT);
//			} else {
//				/* 无水：停止加热 */
//				DRIVER_SENSER_HeatSetDuty(SENSER_HEAT_CH_1, 0U);
//			}
		}
		/* 200ms控制周期：读温度→闭环更新→刷新OLED */
		if(user1TaskInfo.user1TaskCnt % 100U == 0U){
			uint8_t xNext;

			/* 1. 读取水温（阻塞约100ms） */
			DRIVER_SENSER_DS18B20GetTemp();

			/* 2. 水温闭环控制，目标35℃ */
			DRIVER_SENSER_WaterTempCtrlUpdate(30.0f);

			/* 3. OLED显示当前温度与目标温度 */
			DRIVER_OLED_ShowString(0U, 0U, "Now:");
			xNext = DRIVER_OLED_ShowFloat(24U, 0U, senserInfo.ds18b20Temp, 1U);
			DRIVER_OLED_ShowChar6x8(xNext, 0U, 'C', 1U);
			DRIVER_OLED_ShowString(0U, 8U, "Tgt:");
			xNext = DRIVER_OLED_ShowFloat(24U, 8U, senserInfo.waterTempCtrl.targetTemp, 1U);
			DRIVER_OLED_ShowChar6x8(xNext, 8U, 'C', 1U);
		}

		osDelay(2);
	}
}
