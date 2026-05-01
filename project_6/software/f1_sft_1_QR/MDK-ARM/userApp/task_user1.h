#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"
#include <stdint.h>

typedef struct user1TaskInfo {
  uint32_t user1TaskCnt;
} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;

extern void user1TaskInit(void);
extern void user1TaskUpdata(void *argument);

#endif
