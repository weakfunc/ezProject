#include "task_user1.h"
#include "func_appcom.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_steer.h"
#include "driver_imu901.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
  DRIVER_IMU901_Init();
}

void user1TaskUpdata(void *argument){
  user1TaskInit();
  for(;;){
    user1TaskInfo.user1TaskCnt++;

    if(user1TaskInfo.user1TaskCnt % 5 == 0){
      /* 10ms TASK*/
      DRIVER_IMU901_Update();
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
      boardInfo.buzzTimeMs = 200;
    }

    osDelay(2);
  }
}
