#ifndef __TASK_SYSTEM_H__
#define __TASK_SYSTEM_H__

#include "main.h"
#include "cmsis_os.h"
#include "stdlib_usart.h"
#include "stdlib_tim.h"
#include "stdlib_i2c.h"

typedef struct usartInfo{
	uint8_t rxBuff[64];
	uint8_t txBuff[64];
	
	uint8_t cmd;
	uint8_t len;
}usartInfo_t;

typedef struct systemTaskInfo{
	uint32_t systemTaskCnt;					
	
}systemTaskInfo_t;
extern systemTaskInfo_t systemTaskInfo;
extern void systemTaskInit(void);
extern void systemTaskUpdata(void *argument);

#endif
