#include "motorTask.h"

uint32_t time[2];			//舵机在打开状态下的停留时间
uint8_t first[3];			//作用是只让sevroControl(x, SEVRO_OPEN)执1次，我们只需要装填1次PWM值舵机就能到指定位置，不要循环装填
uint32_t i;

//舵机控制，控制舵机1或舵机2开关
void sevroControl(uint8_t sevroId, uint32_t pulse){
	if(sevroId == 1) SET_PWM1_POS(pulse);
	else if(sevroId == 2) SET_PWM2_POS(pulse);
	else if(sevroId == 3) SET_MOTOR_SPEED(pulse);			//传输带转速
}

void motorTaskUpdata(){
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_3);		//电机pwm
	HAL_TIM_PWM_Start(&htim3,TIM_CHANNEL_4);		//舵机1
	HAL_TIM_PWM_Start(&htim3,TIM_CHANNEL_3);		//舵机2
	
	//电机PWM，频率1khz，满占空比是1000，给500就是50%，电机模块电压是12V，应该输出12*0.5=6V
	SET_MOTOR_DIR(1);
	SET_MOTOR_SPEED(1000);
	
	//舵机pwm，频率50hz，满占空比2000，给200就是10%，引脚应该输出3.3*0.1 = 0.33V
	SET_PWM1_POS(SEVRO_CLOSE);
	SET_PWM2_POS(SEVRO_CLOSE);
	for(;;){
		
		if(sysConfig.key1_push) {
			if(i<1000) i++;					//传送带缓启动,如果直接给1000会有一个瞬时压降让整个系统复位
			else i = 1000;
			SET_MOTOR_SPEED(i);
		}else{
			SET_MOTOR_SPEED(0);
			i=0;
		}
		
		if(sysConfig.key1_push){
			if(sysConfig.sevroEnable[0] == 1){			//舵机1动作
				if(first[0] == 0) 
					sevroControl(1, SEVRO_OPEN);
				first[0] = 1;
				time[0]++;
				
				if(time[0] > 500){											//在打开状态停留500ms
					sysConfig.sevroEnable[0] = 0;					//复位标志位
					time[0] = 0;
					first[0] = 0;
					sevroControl(1, SEVRO_CLOSE);
				}					
			}	
			
			if(sysConfig.sevroEnable[1] == 1){			//舵机2动作
				if(first[1] == 0) 
					sevroControl(2, SEVRO_OPEN);
				first[1] = 1;
				time[1]++;
				
				if(time[1] > 500){											//在打开状态停留500ms
					sysConfig.sevroEnable[1] = 0;					//复位标志位
					time[1] = 0;
					first[1] = 0;
					sevroControl(2, SEVRO_CLOSE);
				}					
			}	
		}else{
			if(first[2] == 1){
				SET_MOTOR_SPEED(0);
				first[2] = 0;
			}
		}
		
		osDelay(1);
	}
}
