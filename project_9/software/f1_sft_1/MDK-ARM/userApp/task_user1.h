#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"

/*============================================================================
 * 向上提供：液位测量系统任务信息结构体及接口
 *============================================================================*/

/* 液位测量系统任务信息结构体 */
typedef struct {
  uint32_t taskCnt;               /* 任务计数，用于取余调度 */

  /* 测距原始数据 */
  float distToGround_mm;          /* 普通超声波：模块距地面距离（mm），上电标定后固定 */
  float distToLiquid_mm;          /* 高精度超声波：模块距液面距离（mm），实时更新 */

  /* 液位计算结果 */
  float liquidLevel_mm;           /* 当前液位高度（mm） */
  float liquidLevelInit_mm;       /* 初始液位高度（mm），标定完成时记录 */
  float liquidLevelDelta_mm;      /* 液位变化量（mm）= liquidLevel - liquidLevelInit */

  /* 报警阈值（从 APP 接收，OLED 同步显示） */
  float alarmHigh_mm;             /* 液位高报警阈值（mm），默认 0（未设置） */
  float alarmLow_mm;              /* 液位低报警阈值（mm），默认 0（未设置） */

  /* 标定状态 */
  uint8_t calibDone;              /* 标定完成标志（0=校准中，1=正常工作） */
  uint8_t calibCnt;               /* 已采集标定样本数 */
  uint8_t calibTimeoutCnt;        /* 标定超时计数（每50ms加1，达上限时用默认地面距离） */
  float   calibSum;               /* 标定累加和 */

  /* 滑动均值滤波缓冲区（窗口=5） */
  float   filterBuf[5];           /* 最近5次有效液位高度值 */
  uint8_t filterIdx;              /* 滑动写入索引 */
  uint8_t filterFull;             /* 缓冲区已填满标志（1=已满） */

} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;

void user1TaskInit(void);
void user1TaskUpdata(void *argument);

#endif
