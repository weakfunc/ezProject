#include "sys.h"

uint8_t rxBuffer[9];
uint8_t txBuffer[20] = "hello world";
uint32_t taskCnt;
sysConfig_t sysConfig;
k210RevPack_t k210RevPack;

uint8_t rxByte;                        // 单字节接收缓冲
uint8_t uartRxBuffer[18]; // 环形缓冲区
uint8_t rxIndex = 0;

void systemTaskInit(){
	LCD_Init();//LCD初始化
	LCD_Fill(0,0,LCD_W,LCD_H,BLACK);	
	
	HAL_UART_Receive_IT(&huart1, (uint8_t *)&rxBuffer, 1);
}

void uartConnectCheck(){
	if(sysConfig.lastTime != k210RevPack.time){
		sysConfig.isConnect = 1;
		sysConfig.lastTime = k210RevPack.time;
	}else{
		sysConfig.isConnect = 0;
	}
}

void uartRevPack(){
	for(uint8_t i = 0; i<=18-9; i++){
		if(uartRxBuffer[i] == 0xFF && uartRxBuffer[i+8] == 0xFE){
      k210RevPack.dx = ((int16_t)uartRxBuffer[i + 2] << 8) | uartRxBuffer[i + 1];
      k210RevPack.dy = ((int16_t)uartRxBuffer[i + 4] << 8) | uartRxBuffer[i + 3];
      k210RevPack.time = ((uint16_t)uartRxBuffer[i + 6] << 8) | uartRxBuffer[i + 5];
      k210RevPack.type = uartRxBuffer[i + 7];
		}
	}
}

void systemTaskUpdata(void *argument){
	systemTaskInit();

  for(;;){
		taskCnt++;
		
		if(BOTTON1_PUSH){
			sysConfig.button1_push = 1;
			sysConfig.button2_push = 0;
		}
		
		if(BOTTON2_PUSH){
			sysConfig.button2_push = 1;
		}
		
		if(taskCnt % 100 == 0){
			LCD_ShowString(20,10,"Aim:",WHITE,BLACK,12,0);
			LCD_ShowString(20,20,"dx",WHITE,BLACK,12,0);
			LCD_ShowIntNum(35,20,k210RevPack.dx, 2, WHITE,BLACK,12);
			LCD_ShowString(80,20,"dy",WHITE,BLACK,12,0);
			LCD_ShowIntNum(95,20,k210RevPack.dy, 2, WHITE,BLACK,12);
			if(sysConfig.isConnect){

				if(k210RevPack.type == 1)
					LCD_ShowString(80,10,"ping pang",WHITE,BLACK,12,0);
				else if(k210RevPack.type == 0)
					LCD_ShowString(80,10,"wang qiu",WHITE,BLACK,12,0);
				else if(k210RevPack.type == 2)
					LCD_ShowString(80,10,"yu mao",WHITE,BLACK,12,0);
			}
		}
		
		if(taskCnt % 150 == 0){
			uartConnectCheck();
		}
		
		if(taskCnt % 5 == 0){
			uartRevPack();
		}
		
    osDelay(1);
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  UNUSED(huart);
	if(huart->Instance == USART1){
		uartRxBuffer[rxIndex++] = rxByte;
		if (rxIndex >= 18) rxIndex = 0;
		
		HAL_UART_Receive_IT(huart, &rxByte, 1);
	}
}

int fputc(int ch, FILE *f){
		HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
		return ch;
}

