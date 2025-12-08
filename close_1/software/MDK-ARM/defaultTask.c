#include "defaultTask.h"
#include "stdio.h"
#include "string.h"

sysConfig_t sysConfig;
QRcodePack_t QRcodePack;
QRcodePack_t packList[PACK_MAX_NUM];	 	//本地包裹列表

uint8_t rx2Buffer[UART_RX_BUFFER_SIZE];
uint8_t rx3Buffer[UART_RX_BUFFER_SIZE];
uint8_t rx2Byte; 
uint8_t rx3Byte; 
uint8_t rx2Index; 
uint8_t rx3Index; 
uint8_t tx2Buffer[64] = "hello world";

formatTrans_t test;

void blueToothSend(){
	//蓝牙发送。由于APP不能解析hex数据，要把所有的数据转成字符串发送
	//app收到数据包后直接显示出来即可
	uint8_t index = 0;
	char str[20];
	
	strcpy(str, "id:");  
	for(uint8_t i=0; i<3; i++){
		tx2Buffer[index++] = str[i];
	}
	switch(packList[sysConfig.packNum].id){
		case 1: tx2Buffer[index++] = '1'; break;
		case 2: tx2Buffer[index++] = '2'; break;
		case 3: tx2Buffer[index++] = '3'; break;
		case 4: tx2Buffer[index++] = '4'; break;
		case 5: tx2Buffer[index++] = '5'; break;
		default :break;
	}
	strcpy(str, "    :");  
	for(uint8_t i=0; i<4; i++){
		tx2Buffer[index++] = str[i];
	}
	
	strcpy(str, "aim:");  
	for(uint8_t i=0; i<4; i++){
		tx2Buffer[index++] = str[i];
	}
	switch(packList[sysConfig.packNum].aim){
		case 1: tx2Buffer[index++] = 'A'; break;
		case 2: tx2Buffer[index++] = 'B'; break;
		case 3: tx2Buffer[index++] = 'C'; break;
		default :break;
	}
	strcpy(str, "    :");  
	for(uint8_t i=0; i<4; i++){
		tx2Buffer[index++] = str[i];
	}
	
	
	strcpy(str, "source:");  
	for(uint8_t i=0; i<7; i++){
		tx2Buffer[index++] = str[i];
	}
	switch(packList[sysConfig.packNum].source){
		case 1: tx2Buffer[index++] = 'E'; break;
		case 2: tx2Buffer[index++] = 'F'; break;
		case 3: tx2Buffer[index++] = 'G'; break;
		default :break;
	}
	strcpy(str, "    :");  
	for(uint8_t i=0; i<4; i++){
		tx2Buffer[index++] = str[i];
	}
	
	strcpy(str, "weight:");  
	for(uint8_t i=0; i<7; i++){
		tx2Buffer[index++] = str[i];
	}
	sprintf(str, "%.2f", packList[sysConfig.packNum].weight);  // 保留两位小数
	for(uint8_t i=0; i<4; i++){
		tx2Buffer[index++] = str[i];
	}
	strcpy(str, "    :");  
	for(uint8_t i=0; i<4; i++){
		tx2Buffer[index++] = str[i];
	}
	
	strcpy(str, "time:");  
	for(uint8_t i=0; i<5; i++){
		tx2Buffer[index++] = str[i];
	}
	sprintf(str, "%d", packList[sysConfig.packNum].importTime);  // 保留两位小数
	for(uint8_t i=0; i<4; i++){
		tx2Buffer[index++] = str[i];
	}
//		//把float型的wight数据拆分成4个单字节发送
//		formatTrans_t wight;
//		wight.float_temp = packList[sysConfig.packNum].weight;
//		for(uint8_t i=0; i<4; i++){
//			tx2Buffer[index++] = wight.u8_temp[i];
//		}
//		//把uint32型的importTime数据拆分成4个单字节发送
//		formatTrans_t importTime;
//		importTime.u32_temp = packList[sysConfig.packNum].importTime;
//		for(uint8_t i=0; i<4; i++){
//			tx2Buffer[index++] = importTime.u8_temp[i];
//		}
	HAL_UART_Transmit(&huart3, (uint8_t *)&tx2Buffer, 52, 0xFFFF);
}

void uartRevPack(){
	if(QRcodePack.revFlag == 1){			//如果有新的数据到来
		QRcodePack.revFlag = 0; 			  //先清除标志位

		//储存解析到的数据包到本地包裹列表
		packList[sysConfig.packNum].id = QRcodePack.id;
		packList[sysConfig.packNum].aim = QRcodePack.aim;
		packList[sysConfig.packNum].source = QRcodePack.source;
		packList[sysConfig.packNum].weight = QRcodePack.weight;
		
		packList[sysConfig.packNum].importTime = sysConfig.taskCnt;			//记录包裹入站时间
		switch(packList[sysConfig.packNum].aim){
			case 0x01:{				//目标地是A
				packList[sysConfig.packNum].outportTime = sysConfig.taskCnt + 80;
				packList[sysConfig.packNum].sevroId = 1;
				break;
			}	 		
			case 0x02:{				//目标地是B
				packList[sysConfig.packNum].outportTime = sysConfig.taskCnt + 150;
				packList[sysConfig.packNum].sevroId = 2;
				break;
			}	 		
			case 0x03:{				//目标地是C
				packList[sysConfig.packNum].outportTime = sysConfig.taskCnt + 999;
				packList[sysConfig.packNum].sevroId = 0;					//目标地C不用控制舵机
				break;
			}
			default:{
				SET_RED(GPIO_PIN_RESET);			//数据解析错误，亮红灯
				break;
			}	 		
		}
		blueToothSend();

		sysConfig.packNum++;						//本地包裹总数++
		if(sysConfig.packNum >= 20) sysConfig.packNum = 0;
	}
}

void sysControl(){
	for(uint8_t i=0; i<sysConfig.packNum; i++){			//遍历本地包裹列表
		//如果有包裹未出站，且已经到出站时间，则控制舵机出站
		if(packList[i].isOutFlag == 0 && packList[i].outportTime == sysConfig.taskCnt){
			if(packList[i].sevroId == 1) sysConfig.sevroEnable[0] = 1;				//通知舵机1动作
			else if(packList[i].sevroId == 2) sysConfig.sevroEnable[1] = 1;		//通知舵机2动作
			else if(packList[i].sevroId == 3) break;													//目标地C不用控制舵机
			
			packList[i].isOutFlag = 1;  	//置位outPortFlag就视为已出站
		}
	}
}

//oled显示函数
void oledControl(){
	OLED_Refresh();
	OLED_ShowString(5,5,"packge Num:",8,1);
	OLED_ShowNum(80, 5, sysConfig.packNum, 2, 8, 1);
	OLED_ShowString(5,15,"system time:",8,1);
	OLED_ShowNum(80, 15, sysConfig.taskCnt/10, 3, 8, 1);
	OLED_DrawPoint(100,20,1);					//小数点
	OLED_ShowNum(102, 15, sysConfig.taskCnt%10, 1, 8, 1);
	OLED_ShowString(110,15,"s",8,1);
	
	OLED_ShowString(5,25,"----Packge Info----",8,1);
	OLED_ShowNum(100, 35, QRcodePack.id, 1, 24, 1);			//包裹编号
	
	OLED_ShowString(5,35,"source:",8,1);
	switch(QRcodePack.source){
		case 0x01: OLED_ShowString(50,35,"E",8,1); break;
		case 0x02: OLED_ShowString(50,35,"F",8,1); break;
		case 0x03: OLED_ShowString(50,35,"G",8,1); break;
	}
	OLED_ShowString(5,45,"aim:",8,1);
	switch(QRcodePack.aim){
		case 0x01: OLED_ShowString(30,45,"A",8,1); break;
		case 0x02: OLED_ShowString(30,45,"B",8,1); break;
		case 0x03: OLED_ShowString(30,45,"C",8,1); break;
	}
	OLED_ShowString(5,55,"weight:",8,1);
	OLED_ShowNum(50, 55, (uint32_t)QRcodePack.weight, 2, 8, 1);
	OLED_DrawPoint(65,60,1);					//小数点
	OLED_ShowNum(67, 55, (QRcodePack.weight - (uint32_t)QRcodePack.weight)*100, 2, 8, 1);
	OLED_ShowString(80,55,"kg",8,1);
}

void sysTaskUpdata(){
	//高电平熄灭RGB
	SET_RED(GPIO_PIN_SET);
	SET_GREEN(GPIO_PIN_SET);
	SET_BLUE(GPIO_PIN_SET);
	
	OLED_Init();
	OLED_ColorTurn(0);//0正常显示，1 反色显示
  OLED_DisplayTurn(0);//0正常显示 1 屏幕翻转显示
	
	//串口初始化
	//扫码机串口数据包格式：
	//包头(3字节 0x55,0xAA,0x30)+扫码机自定义数据(无法修改,含义未知,3字节)+包裹信息(编号:1字节 始发地:1字节 目的地:1字节 重量:4字节)+包尾(2字节 扫码机自定义,含义未知)
	//数据包长度15
	HAL_UART_Receive_IT(&huart3, &rx3Buffer[0], 15);		//串口3是扫码机
	HAL_UART_Receive_IT(&huart2, &rx2Buffer[0], 1);		//串口2是蓝牙
	
	//这里应该是发送蓝牙数据用串口2，但是串口2的发送用不了，不知道是什么原因，代码没有问题，但是用逻辑分析仪看不到发送的波形。
	//就改用串口3发送蓝牙数据，串口3是扫码机。扫码机只需要接收不需要发送。正好借用串口3的发送发蓝牙数据
	HAL_UART_Transmit(&huart3, (uint8_t *)&tx2Buffer, sizeof(tx2Buffer), 0xFFFF);
	
	test.float_temp = 15.0f;
	for(;;){
		
		//每隔1s翻转一次LED
		if(sysConfig.taskCnt % 10 == 0){
			TOGGLE_GREEN;
		}
		
		//0.1s刷新一次oled
		if(sysConfig.taskCnt % 1 == 0){
			oledControl();
		}
		
		if(KEY1_PUSH){
			sysConfig.key1_push = 1;				
		}
		if(KEY2_PUSH){		//第二个按键的作用就是清除key1标志位
			sysConfig.key1_push = 0;	
		}
		if(KEY3_PUSH){
			sysConfig.key3_push = 1;
		}
		
		uartRevPack();
		sysControl();
		
		//任务计数器，用于控制子任务的执行周期，比如上面的每0.1s刷新一次oled
		sysConfig.taskCnt++;
		//任务的执行周期是100ms，也就是说这个for循环每100ms执行一次
		osDelay(100);
	}
}

  
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  UNUSED(huart);
	
  if(huart->Instance == USART3){
		if(rx3Buffer[0] == 0x55 && rx3Buffer[1] == 0xAA){
			QRcodePack.id = rx3Buffer[6];
			QRcodePack.source = rx3Buffer[7];
			QRcodePack.aim = rx3Buffer[8];
			
			//接收float型数据weight
			//stm32中数据存储采用小端模式。低字节存储在低地址。
			//也就是说，4字节的浮点数，小数部分(低字节)占了2字节存储在uint8_t[0,1](低地址)
			//整数部分(高字节)占了2字节存储在uint8_t[2,3](高地址)
			//所以，在写二维码信息的时候，也要遵循小端模式，才能适用以下代码
			formatTrans_t weiht;
			for(uint8_t i=0; i<4; i++){
				weiht.u8_temp[i] = rx3Buffer[9+i];
			}
			QRcodePack.weight = weiht.float_temp;
			
			QRcodePack.revFlag = 1;					//收到一包数据，标志位置1
		}
		HAL_UART_Receive_IT(huart, &rx3Buffer[0], 15);
	}
	
	if(huart->Instance == USART2){
		sysConfig.key1_push = rx2Byte;		//app只发来一个字节用于控制传送带是否启动，只需要把这个字节赋给key1_push即可
		HAL_UART_Receive_IT(huart, &rx2Byte, 1);
	}
}
