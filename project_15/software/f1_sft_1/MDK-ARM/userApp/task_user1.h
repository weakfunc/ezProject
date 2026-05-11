#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"

typedef struct user1TaskInfo{
	uint32_t user1TaskCnt;

	float yawRef;    /* 摄像头模式：屏幕角度参考，单位：度 */
	float picthRef;

	float xRef;      /* GotoXY 模式：光斑 X 坐标，范围 [-100, +100] */
	float yRef;      /* GotoXY 模式：光斑 Y 坐标，范围 [-100, +100] */
}user1TaskInfo_t;

extern void user1TaskInit(void);
extern void user1TaskUpdata(void *argument);

#endif

