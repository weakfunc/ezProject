#ifndef __defaultTask_h__
#define __defaultTask_h__
#include "cmsis_os.h"
#include "main.h"
#include "oled.h"
#include "usart.h"

#define KEY1_PUSH (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == 0)
#define KEY2_PUSH (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == 0)
#define KEY3_PUSH (HAL_GPIO_ReadPin(KEY3_GPIO_Port, KEY3_Pin) == 0)

#define SET_GREEN(x) HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, x)
#define SET_BLUE(x) HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, x)
#define SET_RED(x) HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, x)
#define TOGGLE_GREEN HAL_GPIO_TogglePin(LED_R_GPIO_Port, LED_R_Pin)
#define TOGGLE_BLUE HAL_GPIO_TogglePin(LED_B_GPIO_Port, LED_B_Pin)
#define TOGGLE_RED HAL_GPIO_TogglePin(LED_G_GPIO_Port, LED_G_Pin)

#define UART_RX_BUFFER_SIZE 18
#define PACK_MAX_NUM 20

typedef struct{
	uint32_t taskCnt;
	uint8_t key1_push;
	uint8_t key2_push;
	uint8_t key3_push;
	uint8_t packNum;
	
	uint8_t sevroEnable[2];				//通知舵机动作的标志位
}sysConfig_t;

typedef struct{
	uint8_t id; //编号
	uint8_t source;	//始发地
	uint8_t aim; //目标地
	float weight; //重量
	uint8_t revFlag; //接收到一包数据标志位
	
	uint32_t importTime;		//包裹入站时间
	uint32_t outportTime;		//根据包裹的目标地计算出出站时间	
	uint8_t sevroId;				//到达目标地应该控制的舵机编号
	uint8_t isOutFlag;			//包裹是否已经出站
}QRcodePack_t;

//联合体用于数据类型转换，串口每次只能发1个字节，如果我想发float型数据，就要用这个联合体把4字节的float拆分成4个1字节的uint8发送
typedef union{
	uint8_t u8_temp[4];
	uint16_t u16_temp[2];
	uint32_t u32_temp;
	float float_temp;
}formatTrans_t;

extern sysConfig_t sysConfig;

#endif
