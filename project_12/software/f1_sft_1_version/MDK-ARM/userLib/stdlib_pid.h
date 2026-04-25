#ifndef __STDLIB_PID_H__
#define __STDLIB_PID_H__

#include <stdint.h>

/*============================================================================
 * 内部配置
 *============================================================================*/

/* PID实例最大数量，按实际使用数量修改，避免浪费RAM */
#define PID_MAX_COUNT  (8U)

/*============================================================================
 * API接口
 *============================================================================*/

/* PID运行状态结构体，所有实例统一用pidInfo[]数组管理 */
typedef struct {
  /* 增益参数（由Init写入，运行中只读） */
  float kp;      /* 比例增益 */
  float ki;      /* 积分增益 */
  float kd;      /* 微分增益 */
  float pMax;    /* P项输出限幅 */
  float iMax;    /* I项积分限幅（防积分饱和） */
  float outMax;  /* 最终输出限幅 */

  /* 运行状态（由Calc更新） */
  float ref;     /* 目标值 */
  float fdb;     /* 反馈值 */
  float err;     /* 当前误差 */
  float lastErr; /* 上次误差（用于微分项） */
  float pOut;    /* P项输出 */
  float iOut;    /* I项输出（含积分历史） */
  float dOut;    /* D项输出 */
  float output;  /* 最终输出 */
} pidInfo_t;

/* 所有PID实例数组，id为索引，外部可读取各字段用于调试 */
extern pidInfo_t pidInfo[PID_MAX_COUNT];

/* 初始化指定id的PID实例：清零全部状态，写入增益及限幅参数 */
void STDLIB_PID_Init(uint8_t id,
                     float kp, float ki, float kd,
                     float pMax, float iMax, float outMax);

/* 输入目标值ref和反馈值fdb，计算PID输出并返回，同步更新pidInfo[id]各状态字段 */
float STDLIB_PID_Calc(uint8_t id, float ref, float fdb);

#endif
