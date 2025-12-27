#include "pid.h"

pid_struct_t k210Pid[2];
pid_struct_t motorPid[5];

void pid_ZeroSet(pid_struct_t* pid){
	pid->kp = 0;
	pid->ki = 0;
	pid->kd = 0;
	pid->err[0] = 0;
	pid->err[1] = 0;
	pid->p_out = 0;
	pid->i_out = 0;
	pid->d_out = 0;
	pid->i_max = 0;
	pid->out_max = 0;
}

void pid_init(pid_struct_t *pid, float kp, float ki, float kd,float p_max, float i_max, float out_max){
	pid_ZeroSet(pid);
  pid->kp      = kp;
  pid->ki      = ki;
  pid->kd      = kd;
	pid->p_max   = p_max;
  pid->i_max   = i_max;
  pid->out_max = out_max;
}

float pid_calc(pid_struct_t *pid, float ref, float fdb){
	pid->ref = ref;
  pid->fdb = fdb;
  pid->err[1] = pid->err[0];
  pid->err[0] = pid->ref - pid->fdb;
  
  pid->p_out  = pid->kp * pid->err[0];
  pid->i_out += pid->ki * pid->err[0];
  pid->d_out  = pid->kd * (pid->err[0] - pid->err[1]);
	LIMIT_MIN_MAX(pid->p_out, -pid->p_max, pid->p_max);
  LIMIT_MIN_MAX(pid->i_out, -pid->i_max, pid->i_max);
  
	pid->output = pid->p_out + pid->i_out + pid->d_out;
	
  LIMIT_MIN_MAX(pid->output, -pid->out_max, pid->out_max);
  return pid->output;
}


