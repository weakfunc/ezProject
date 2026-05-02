#include "stdlib_pid.h"

/* PID实例数组（全局，供外部读取状态） */
pidInfo_t pidInfo[PID_MAX_COUNT];

/* 输出限幅宏（私有） */
#define __PID_CLAMP(x, lo, hi)  ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

/* 初始化指定id的PID实例，清零运行状态并写入增益参数。 */
void STDLIB_PID_Init(uint8_t id,
                     float kp, float ki, float kd,
                     float pMax, float iMax, float outMax){
  pidInfo_t *p = &pidInfo[id];

  /* 清零运行状态 */
  p->ref     = 0.0f;
  p->fdb     = 0.0f;
  p->err     = 0.0f;
  p->lastErr = 0.0f;
  p->pOut    = 0.0f;
  p->iOut    = 0.0f;
  p->dOut    = 0.0f;
  p->output  = 0.0f;

  /* 写入增益及限幅参数 */
  p->kp      = kp;
  p->ki      = ki;
  p->kd      = kd;
  p->pMax    = pMax;
  p->iMax    = iMax;
  p->outMax  = outMax;
}

/* 计算指定id的PID输出，同步更新pidInfo[id]各状态字段。 */
float STDLIB_PID_Calc(uint8_t id, float ref, float fdb){
  pidInfo_t *p = &pidInfo[id];

  /* 更新目标值、反馈值和误差 */
  p->ref     = ref;
  p->fdb     = fdb;
  p->lastErr = p->err;
  p->err     = ref - fdb;

  /* 计算各项输出 */
  p->pOut  = p->kp * p->err;
  p->iOut += p->ki * p->err;
  p->dOut  = p->kd * (p->err - p->lastErr);

  /* P项和I项分别限幅 */
  p->pOut = __PID_CLAMP(p->pOut, -p->pMax, p->pMax);
  p->iOut = __PID_CLAMP(p->iOut, -p->iMax, p->iMax);

  /* 汇总并对最终输出限幅 */
  p->output = p->pOut + p->iOut + p->dOut;
  p->output = __PID_CLAMP(p->output, -p->outMax, p->outMax);

  return p->output;
}
