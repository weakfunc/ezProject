#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"

typedef struct user1TaskInfo{
	uint32_t user1TaskCnt;

	float yawRef;
	float picthRef;
}user1TaskInfo_t;

extern void user1TaskInit(void);
extern void user1TaskUpdata(void *argument);

#endif

