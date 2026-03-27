#ifndef __motorTask_h__
#define __motorTask_h__
#include "cmsis_os.h"
#include "main.h"
#include "defaultTask.h"
#include "tim.h"

#define SET_MOTOR_DIR(x) HAL_GPIO_WritePin(DIR_A_GPIO_Port, DIR_A_Pin, x);HAL_GPIO_WritePin(DIR_B_GPIO_Port, DIR_B_Pin, !x);
#define SET_MOTOR_SPEED(x) __HAL_TIM_SetCompare(&htim4, TIM_CHANNEL_3, x)
#define SET_PWM1_POS(x) __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_3, x)
#define SET_PWM2_POS(x) __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_4, x)


#define SEVRO_CLOSE 200
#define SEVRO_OPEN 100


#endif
