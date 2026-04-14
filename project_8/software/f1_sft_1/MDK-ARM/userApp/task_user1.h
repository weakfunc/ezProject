#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"
#include <stdint.h>

/*============================================================================
 * 向上提供（公有结构体、API）
 *============================================================================*/

/**
 * user1任务信息结构体
 * 管理温度采集、按键交互、报警、电池电量、休眠等所有应用状态
 */
typedef struct {
  uint32_t taskCnt;            /* 任务计数器，用于取余获得子周期（2ms基础周期） */

  /* 温度相关 */
  float    tempLatest;         /* 后台每100ms采集的最新目标温度 targetTempC（°C） */
  float    tempDisplay;        /* OLED当前显示的目标温度（仅KEY1按下时从tempLatest更新） */
  float    bodyTempLatest;     /* 后台每100ms采集的最新人体温度 bodyTempC（°C） */
  float    bodyTempDisplay;    /* OLED当前显示的人体温度（仅KEY1按下时从bodyTempLatest更新） */
  int16_t  alarmThreshHighX10; /* 报警上限阈值×10（单位0.1°C），初始值375（=37.5°C），超过则报警 */

  /* 按键边沿检测缓存（与boardInfo.key.pressCount比较，检测新按下事件） */
  uint32_t lastKey1PressCount; /* 上次KEY1按下计数快照 */
  uint32_t lastKey2PressCount; /* 上次KEY2按下计数快照 */
  uint32_t lastKey3PressCount; /* 上次KEY3按下计数快照 */

  /* 按键事件标志（边沿检测后置1，处理后清0） */
  uint8_t  key1Pressed;        /* KEY1触发标志 */
  uint8_t  key2Pressed;        /* KEY2触发标志 */
  uint8_t  key3Pressed;        /* KEY3触发标志 */

  /* 报警相关 */
  uint8_t  alarmActive;        /* 当前报警状态（1=报警中，0=正常） */

  /* 电池相关 */
  uint8_t  battPercent;        /* 电池电量百分比（0~100） */

  /* 显示保持计时 */
  uint32_t displayHoldCnt;     /* KEY1按下后的显示保持计数（2ms计，>=5000为10s已满） */

  /* 休眠计时 */
  uint32_t idleCnt;            /* 无操作计时（2ms计，>=15000为30s触发休眠） */
  uint8_t  sleepFlag;          /* 休眠状态标志（1=休眠中，0=正常运行） */

  /* 温度有效性标志 */
  uint8_t  hasValidTemp;       /* 是否已获得至少一次有效温度读数（1=是，0=否） */

} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;

/* 初始化user1任务（由user1TaskUpdata内部调用） */
void user1TaskInit(void);

/* user1任务主循环（FreeRTOS任务入口，基础周期2ms） */
void user1TaskUpdata(void *argument);

#endif
