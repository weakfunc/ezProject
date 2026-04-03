#include "task_user1.h"
#include "driver_ble.h"

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
