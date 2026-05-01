#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"

/*============================================================================
 * 跌倒检测阈值
 *============================================================================*/
/* 冲击阈值（单位：g），兼顾手持震动测试与误触发抑制 */
#define FALL_THRESHOLD_HIGH    1.6f
/* 确认阈值（单位：g），震动后短暂回落即可确认 */
#define FALL_THRESHOLD_STATIC  2.0f
/* 自动复位阈值（单位：g），保持较高避免刚触发后被轻微晃动清除 */
#define FALL_RESET_THRESHOLD   3.0f
/* 5 × 10ms = 50ms 确认时长 */
#define FALL_CONFIRM_COUNT     5U

/*============================================================================
 * 行走引导参数
 *============================================================================*/
/* 触发行走引导的障碍物距离阈值（cm） */
#define GUIDE_OBSTACLE_CM         30.0f
/* 判定完成转向的 yaw 变化量（°） */
#define GUIDE_TURN_THRESHOLD_DEG  20.0f

/*============================================================================
 * 摔倒报警手机号（修改此宏更换预设联系人）
 *============================================================================*/
#define ALERT_PHONE_NUMBER   "13380114403"

/*============================================================================
 * task 公有信息结构体
 *============================================================================*/
typedef struct {
  uint32_t taskCnt;          /* 任务计数，用于多周期调度 */

  float    distance_cm;      /* 当前超声波测距值（cm） */
  uint8_t  distanceValid;    /* 测距有效标志：1=有效，0=无效 */

  uint8_t  isFallen;         /* 摔倒标志：1=已摔倒，0=正常 */
  uint8_t  fallAlertSent;    /* 摔倒短信已发标志：1=已发，防重复 */

  uint8_t  gpsFixed;         /* GPS 有效定位标志：1=已定位 */
  float    latitude;         /* GPS 纬度（十进制度，南纬为负） */
  float    longitude;        /* GPS 经度（十进制度，西经为负） */

  uint8_t  lastHour;         /* 上次整点报时的小时数（0xFF=无效初值） */
  uint8_t  voiceBusy;        /* 语音冷却忙标志：1=忙，0=空闲 */
  uint8_t  oledNeedUpdate;   /* OLED 待刷新标志 */
} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;
void user1TaskInit(void);
void user1TaskUpdata(void *argument);

#endif
