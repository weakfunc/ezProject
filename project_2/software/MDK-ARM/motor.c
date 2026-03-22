#include "motor.h"


motorConfig_t motorConfig = {
	.axisXref = 15,					//距目标左右偏移
	.axisYref = 100,				//距目标前后间距		
};

void motorTaskInit(){
	HAL_TIMEx_PWMN_Start(&htim1,TIM_CHANNEL_1);
	HAL_TIMEx_PWMN_Start(&htim1,TIM_CHANNEL_3);
	HAL_TIM_PWM_Start(&htim2,TIM_CHANNEL_1); 
	HAL_TIM_PWM_Start(&htim2,TIM_CHANNEL_2); 
	HAL_TIM_PWM_Start(&htim2,TIM_CHANNEL_3); 
	SET_MOTOR_A_DIR(1);
	SET_MOTOR_B_DIR(1);
	SET_MOTOR_C_DIR(1);
	SET_MOTOR_D_DIR(1);
	SET_MOTOR_E_DIR(0);
	SET_MOTOR_A_SPEED(SPPEE_ZERO);
	SET_MOTOR_B_SPEED(SPPEE_ZERO);
	SET_MOTOR_C_SPEED(SPPEE_ZERO);
	SET_MOTOR_D_SPEED(SPPEE_ZERO);
	SET_MOTOR_E_SPEED(100);
	pid_init(&k210Pid[0], 2, 0.5 , 0, 120, 30, 150);	//x轴pid
	pid_init(&k210Pid[1], 8, 0.2 , 0, 150, 50, 200);		//y轴pid
	pid_init(&motorPid[4], 10, 0.2, 0, 350, 50, 400);
	motorConfig.speedRef[4] = 20;
}

void chassicControl(float vx, float vy, float vz){
	motorConfig.speedRef[0] = vx - vy + vz * (H/2 + W/2);
	motorConfig.speedRef[1] = vx + vy - vz * (H/2 + W/2);
	motorConfig.speedRef[2] = vx + vy + vz * (H/2 + W/2);
	motorConfig.speedRef[3] = vx - vy - vz * (H/2 + W/2);
	
	LIMIT(motorConfig.speedRef[0], -SPEED_MAX, SPEED_MAX);
	LIMIT(motorConfig.speedRef[1], -SPEED_MAX, SPEED_MAX);
	LIMIT(motorConfig.speedRef[2], -SPEED_MAX, SPEED_MAX);
	LIMIT(motorConfig.speedRef[3], -SPEED_MAX, SPEED_MAX);
	
	if(motorConfig.speedRef[0] < 0){
		motorConfig.dir[0] = 0;
		motorConfig.speedRef[0] = -motorConfig.speedRef[0];
	}else{
		motorConfig.dir[0] = 1;
	}
	if(motorConfig.speedRef[1] < 0){
		motorConfig.dir[1] = 0;
		motorConfig.speedRef[1] = -motorConfig.speedRef[1];
	}else{
		motorConfig.dir[1] = 1;
	}
	if(motorConfig.speedRef[2] < 0){
		motorConfig.dir[2] = 0;
		motorConfig.speedRef[2] = -motorConfig.speedRef[2];
	}else{
		motorConfig.dir[2] = 1;
	}
	if(motorConfig.speedRef[3] < 0){
		motorConfig.dir[3] = 0;
		motorConfig.speedRef[3] = -motorConfig.speedRef[3];
	}else{
		motorConfig.dir[3] = 1;
	}
	
	SET_MOTOR_A_DIR(motorConfig.dir[0]);
	SET_MOTOR_B_DIR(motorConfig.dir[1]);
	SET_MOTOR_C_DIR(motorConfig.dir[2]);
	SET_MOTOR_D_DIR(motorConfig.dir[3]);
	SET_MOTOR_A_SPEED(motorConfig.speedRef[0]);
	SET_MOTOR_B_SPEED(motorConfig.speedRef[1]);
	SET_MOTOR_C_SPEED(motorConfig.speedRef[2]);
	SET_MOTOR_D_SPEED(motorConfig.speedRef[3]);
}

void gunlunControl(){
	pid_calc(&motorPid[4], motorConfig.speedRef[4], motorConfig.speedFbd[4]);	
	if(motorPid[4].output<0) motorPid[4].output = 0;
	SET_MOTOR_E_SPEED(motorPid[4].output);
}

void getSpeed(){
	motorConfig.speedFbd[4] = motorConfig.econder[4] - motorConfig.last_econder[4];
	motorConfig.last_econder[4] = motorConfig.econder[4];
}

void avoidance(){
	if(motorConfig.echoTime < 500){
		motorConfig.vx = 0;
		motorConfig.vy = 0;
		motorConfig.vz = 1000;
	}else{
		motorConfig.vx = 150;
		motorConfig.vy = 0;
		motorConfig.vz = 0;
	}
	chassicControl(motorConfig.vx, motorConfig.vy, motorConfig.vz);
}

void autoFind(){
	static uint8_t step = 0;
	static uint32_t time = 0;
	switch(step){
		case 0:{
			motorConfig.vx = 0;
			motorConfig.vy = 0;
			motorConfig.vz = 1000;
			chassicControl(motorConfig.vx, motorConfig.vy, motorConfig.vz);
			time++;
			if(time > 1500){
				time = 0;
				step++;
			}
			break;
		}
		
		case 1:{
			avoidance();
			time++;
			if(time > 2000){
				time = 0;
				step++;
			}
			break;
		}		
		default:{
			step = 0;
			break;
		}
	}
	chassicControl(motorConfig.vx, motorConfig.vy, motorConfig.vz);
}
void getPingPang(){
	if(motorConfig.getRubbishTime > 3000){				
		motorConfig.getPingPangFlag = 0;
		motorConfig.getRubbishTime = 0;	
		motorConfig.vx = 0;
		motorConfig.vy = 0;
		motorConfig.vz = 0;
		motorConfig.getRubbishTime++;
		chassicControl(motorConfig.vx, motorConfig.vy, motorConfig.vz);
	}else{
		motorConfig.vx = 100;
		motorConfig.vy = 0;
		motorConfig.vz = 0;
		motorConfig.getRubbishTime++;
		
		chassicControl(motorConfig.vx, motorConfig.vy, motorConfig.vz);
	}
}

void HC_SR04_Delayus(uint32_t usdelay)
{
  __IO uint32_t Delay = usdelay * (SystemCoreClock / 8U / 1000U/1000);//SystemCoreClock:系统频率
  do
  {
    __NOP();
  }
  while (Delay --);
}

void motorTaskUpdata(void *argument){
	motorTaskInit();
	//sysConfig.button2_push = 1;
  for(;;){
		
		if(sysConfig.button1_push == 1){
			sysConfig.button2_push = 0;
			sysConfig.button1_push = 0;
			motorConfig.vx = 0;
			motorConfig.vy = 0;
			motorConfig.vz = 0;
			SET_MOTOR_E_SPEED(0);
			chassicControl(motorConfig.vx, motorConfig.vy, motorConfig.vz);
		}
		
		if(sysConfig.button2_push == 1){
			if(motorConfig.getPingPangFlag == 1){
				getPingPang();
			}else if(sysConfig.isConnect){
				pid_calc(&k210Pid[0], motorConfig.axisXref, k210RevPack.dx);			
				pid_calc(&k210Pid[1], motorConfig.axisYref, k210RevPack.dy);				 //目标离Y中心越远，反馈值越小。目标在屏幕最顶端，dy=-120，目标在屏幕最底端，dy=120
				motorConfig.vy = k210Pid[0].output;
				motorConfig.vx = k210Pid[1].output;
				motorConfig.vz = 0;
				chassicControl(motorConfig.vx, motorConfig.vy, motorConfig.vz);
				
				if(k210RevPack.type == 0){				//识别到网球
					if(fabs(k210RevPack.dx - motorConfig.axisXref) < THRESHOLD_DX && fabs(k210RevPack.dy - motorConfig.axisYref) < THRESHOLD_DY){					//目标接近期望，小于阈值时开启夹取流程
						motorConfig.getWangqiuFlag = 1;
					}
				}else if(k210RevPack.type == 1){	//识别到乒乓球
					if(fabs(k210RevPack.dx - motorConfig.axisXref) < THRESHOLD_DX && fabs(k210RevPack.dy - motorConfig.axisYref) < 90){					//目标接近期望，小于阈值时开启夹取流程
						motorConfig.getPingPangFlag = 1;
					}
				}else if(k210RevPack.type == 2){  //识别到羽毛球
					if(fabs(k210RevPack.dx - motorConfig.axisXref) < THRESHOLD_DX && fabs(k210RevPack.dy - motorConfig.axisYref) < 90){					//目标接近期望，小于阈值时开启夹取流程
						motorConfig.getPingPangFlag = 1;
					}
				}
			}else{
				autoFind();
			}
			
		}
		gunlunControl();
		if(motorConfig.motorTaskCnt%50 == 0){
			getSpeed();
		}
		
		if(motorConfig.motorTaskCnt%500 == 0){
			SET_TRIG(1);
			HC_SR04_Delayus(20);
			SET_TRIG(0);
		}
		
		
		
		motorConfig.motorTaskCnt++;
    osDelay(1);
  }            
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
	if(GPIO_Pin == FBD1_B_Pin) {
		if(GET_MOTOR_A_DIR) motorConfig.econder[0]++;
		else motorConfig.econder[0]--;
	}
	 
	if(GPIO_Pin == FBD2_B_Pin) {
		if(GET_MOTOR_B_DIR) motorConfig.econder[1]++;
		else motorConfig.econder[1]--;
	}
	
	if(GPIO_Pin == FBD3_B_Pin) {
		if(GET_MOTOR_C_DIR) motorConfig.econder[2]++;
		else motorConfig.econder[2]--;
	}

	if(GPIO_Pin == FBD4_B_Pin) {
		if(GET_MOTOR_D_DIR) motorConfig.econder[3]++;
		else motorConfig.econder[3]--;
	}
	
	if(GPIO_Pin == FBD4_B_Pin) {
		if(GET_MOTOR_D_DIR) motorConfig.econder[3]++;
		else motorConfig.econder[3]--;
	}
	if(GPIO_Pin == FBD5_A_Pin) {
		motorConfig.econder[4]++;
	}
	
	if(GPIO_Pin == GPIO_PIN_3) {
		static uint32_t time = 0;
		while(HAL_GPIO_ReadPin(ECHO_GPIO_Port, ECHO_Pin) == GPIO_PIN_SET){
			time++;
			HC_SR04_Delayus(1);
			if(motorConfig.echoTime >100000) break;
		}
		motorConfig.echoTime = time;
		time = 0;
	}
}
