#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"
#include <stdint.h>

typedef struct {
  uint32_t taskCnt;           /* 任务计数器，用于子周期取余 */

  /* GPS 数据 */
  float gpsLatitude;          /* 纬度（度，浮点） */
  float gpsLongitude;         /* 经度（度，浮点） */
  uint8_t gpsValid;           /* GPS 定位状态（0=无效，1=有效） */
  uint8_t gpsStatus;          /* GPS连接状态，对应gpsStatus_t枚举值 */

  /* 防盗状态 */
  uint8_t antitheftEnabled;   /* 防盗功能开关（0=关闭，1=开启） */
  uint8_t alarmFlag;          /* 报警标志（0=正常，1=报警中） */
  uint8_t alarmSource;        /* 报警来源（0=无，1=震动，2=位移） */

  /* 位移监测 */
  float refLatitude;          /* 防盗开启时记录的参考纬度 */
  float refLongitude;         /* 防盗开启时记录的参考经度 */
  uint8_t refPositionSet;     /* 参考位置是否已记录（0=未记录，1=已记录） */

  /* 休眠管理 */
  uint32_t sleepCountdownMs;  /* 休眠倒计时剩余毫秒数（最大30000） */
  uint8_t isSleeping;         /* 当前是否处于休眠状态（0=正常，1=休眠） */

  /* APP 远程指令缓存 */
  uint8_t appAntitheftCmd;    /* APP 发来的防盗开关指令（0xFF表示无指令） */
  uint8_t appAlarmClearCmd;   /* APP 发来的报警消除指令 */
} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;
extern void user1TaskInit(void);
extern void user1TaskUpdata(void *argument);

#endif
