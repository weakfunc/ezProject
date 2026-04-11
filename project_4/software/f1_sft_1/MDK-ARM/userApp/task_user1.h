#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"

/* 默认目标水温及按摩模式，供跨模块引用默认值时使用。 */
#define USER1_TARGET_TEMP_DEFAULT   (40.0f)
#define USER1_MASSAGE_MODE_STOP     (0U)

typedef struct user1TaskInfo{
	uint32_t taskCnt;            /* 任务计数器，用于 2ms 基准周期分频 */

	float    currentTemp;        /* 当前水温，单位：℃ */
	float    targetTemp;         /* APP 设定的目标水温，单位：℃ */
	float    pidIntegral;        /* PID 积分项累计值 */
	float    pidLastError;       /* PID 上一周期误差 */
	uint8_t  heaterOn;           /* 当前实际加热输出状态：0=关，1=开 */
	uint8_t  overTempFlag;       /* 超温保护标志：0=正常，1=超温 */
	uint8_t  dryBurnFlag;        /* 防干烧标志：0=有水，1=缺水 */

	uint8_t  massageMode;        /* 当前实际按摩模式 */
	uint8_t  massageModeTarget;  /* APP 下发的目标按摩模式 */
	uint32_t motorPwmDuty;       /* 当前电机占空比百分比，0~100 */

	uint32_t timerSetSec;        /* 定时总时长，单位：秒 */
	uint32_t timerRemainSec;     /* 定时剩余时长，单位：秒 */
	uint8_t  timerActive;        /* 定时是否激活：0=否，1=是 */

	uint8_t  sysRunning;         /* 系统运行状态：0=待机，1=运行 */
	uint8_t  faultCode;          /* 故障码：0=正常，1=超温，2=缺水，3=复合故障 */
}user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;
extern void user1TaskInit(void);
extern void user1TaskUpdata(void *argument);

#endif
