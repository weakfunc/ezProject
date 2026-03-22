#ifndef __PID_H
#define __PID_H

#include "stdint.h"

#define LIMIT_MIN_MAX(x,min,max) (x) = (((x)<=(min))?(min):(((x)>=(max))?(max):(x)))

typedef struct{
  float kp;
  float ki;
  float kd;
	float p_max;
  float i_max;
  float out_max;
  
  float ref;      // target value
  float fdb;      // feedback value
  float err[2];   // error and last error

  float p_out;
  float i_out;
  float d_out;
  float output;
}pid_struct_t;

float pid_calc(pid_struct_t *pid, float ref, float fdb);
void pid_init(pid_struct_t *pid, float kp, float ki, float kd,float p_max, float i_max, float out_max);

extern pid_struct_t k210Pid[2];
extern pid_struct_t motorPid[5];

#endif
