#include "task_user1.h"
#include "driver_oled.h"
#include "driver_steer.h"
#include "driver_tb6612.h"
#include "driver_QR.h"
#include "driver_board.h"
#include "driver_verison.h"
#include "driver_GY615.h"
#include "driver_ws2812.h"
#include "driver_mp3.h"
#include "driver_ble.h"
#include "func_appcom.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_BLE_Init();

}

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;


		osDelay(2);
	}
}
