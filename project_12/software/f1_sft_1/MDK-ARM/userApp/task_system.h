#ifndef __TASK_SYSTEM_H__
#define __TASK_SYSTEM_H__

#include "main.h"
#include "cmsis_os.h"
#include "stdlib_usart.h"
#include "stdlib_tim.h"
#include "stdlib_i2c.h"
#include "stdlib_dwt.h"

typedef struct systemTaskInfo{
	uint32_t systemTaskCnt;
	
	uint8_t systemEnable_board;
	uint8_t systemEnable_app;
	uint8_t systemEnable;
	uint8_t systemInitFinshFlag;
	uint8_t key2Press;
	uint8_t key3Press;
	
}systemTaskInfo_t;

extern systemTaskInfo_t systemTaskInfo;

extern void systemTaskInit(void);
extern void systemTaskUpdata(void *argument);

#endif
