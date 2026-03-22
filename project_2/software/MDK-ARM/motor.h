#ifndef __motor_h__
#define __motor_h__
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "tim.h"
#include "gpio.h"
#include "usart.h"
#include "stdio.h"
#include "main.h"
#include "sys.h"
#include "pid.h"

#define SPPEE_ZERO 0
#define SPEED_LOW 300
#define SPEED_MID 400
#define SPEED_HIGH 600
#define SPEED_MAX 500

#define SET_MOTOR_A_DIR(x) HAL_GPIO_WritePin(DIR_M1_GPIO_Port, DIR_M1_Pin, x)
#define SET_MOTOR_B_DIR(x) HAL_GPIO_WritePin(DIR_M2_GPIO_Port, DIR_M2_Pin, x)
#define SET_MOTOR_C_DIR(x) HAL_GPIO_WritePin(DIR_M3_GPIO_Port, DIR_M3_Pin, x)
#define SET_MOTOR_D_DIR(x) HAL_GPIO_WritePin(DIR_M4_GPIO_Port, DIR_M4_Pin, x)
#define SET_MOTOR_E_DIR(x) HAL_GPIO_WritePin(DIR_M5_GPIO_Port, DIR_M5_Pin, x)
#define SET_TRIG(x) HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, x)

#define GET_MOTOR_A_DIR (HAL_GPIO_ReadPin(FBD1_A_GPIO_Port, FBD1_A_Pin) == 1)
#define GET_MOTOR_B_DIR (HAL_GPIO_ReadPin(FBD2_A_GPIO_Port, FBD2_A_Pin) == 1)
#define GET_MOTOR_C_DIR (HAL_GPIO_ReadPin(FBD3_A_GPIO_Port, FBD3_A_Pin) == 1)
#define GET_MOTOR_D_DIR (HAL_GPIO_ReadPin(FBD4_A_GPIO_Port, FBD4_A_Pin) == 1)



#define SET_MOTOR_A_SPEED(x) __HAL_TIM_SetCompare(&htim1, TIM_CHANNEL_3, x)
#define SET_MOTOR_B_SPEED(x) __HAL_TIM_SetCompare(&htim1, TIM_CHANNEL_1, x)
#define SET_MOTOR_C_SPEED(x) __HAL_TIM_SetCompare(&htim2, TIM_CHANNEL_3, x)
#define SET_MOTOR_D_SPEED(x) __HAL_TIM_SetCompare(&htim2, TIM_CHANNEL_2, x)
#define SET_MOTOR_E_SPEED(x) __HAL_TIM_SetCompare(&htim2, TIM_CHANNEL_1, x)

#define LIMIT(value, min, max) ((value) < (min) ? (min) : ((value) > (max) ? (max) : (value)))

#define H 	0.13
#define W  	0.146

#define fabs(x) ((x) < 0 ? -(x) : (x))

#define THRESHOLD_DX	 10
#define THRESHOLD_DY	 20

typedef struct{
	float speedRef[5];
	float speedFbd[5];
	uint8_t dir[4];

	int32_t econder[5];
	int32_t last_econder[5];
	
	int16_t axisXref;
	int16_t axisYref;
	int32_t time;
	uint8_t getWangqiuFlag;
	uint8_t getPingPangFlag;
	uint32_t getRubbishTime;
	
	uint32_t motorTaskCnt;
	
	float vx;
	float vy;
	float vz;
	
	uint8_t echoFlag;
	uint32_t echoTime;
}motorConfig_t;

#endif
