#ifndef __sys_h__
#define __sys_h__
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "tim.h"
#include "gpio.h"
#include "usart.h"
#include "stdio.h"
#include "lcd.h"



#define BOTTON1_PUSH (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == 0)	
#define BOTTON2_PUSH (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == 0)	

#define SET_BEEF(x) HAL_GPIO_WritePin(BEEF_GPIO_Port, BEEF_Pin, x)
#define TOGGLE_BEEF HAL_GPIO_TogglePin(BEEF_GPIO_Port, BEEF_Pin)

#define PACKET_HEADER1 0xFF
#define PACKET_HEADER2 0xFA
#define PACKET_FOOTER  0xAA

typedef struct{
	int16_t lastTime;
	uint8_t isFind;
	uint8_t isConnect;
	uint8_t button1_push;
	uint8_t button2_push;
}sysConfig_t;

typedef struct{
	int16_t dx;
	int16_t dy;
	uint16_t time;
	uint8_t type;
}k210RevPack_t;

extern sysConfig_t sysConfig;
extern k210RevPack_t k210RevPack;

#endif
