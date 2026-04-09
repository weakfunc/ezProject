#include "task_user1.h"
#include "driver_oled.h"
#include "driver_board.h"
#include "driver_gps.h"
#include "driver_mpu6050.h"
#include "driver_sim800c.h"

user1TaskInfo_t user1TaskInfo;

void user1TaskInit(){
	DRIVER_GPS_Init();
	DRIVER_MPU6050_Init();
	DRIVER_OLED_Init();
	DRIVER_SIM800C_Init();
}

float longti, lati;

/* SIM800C 短信发送测试：上电后仅触发一次，发送测试短信到指定号码。 */
static uint8_t smsSentFlag = 0U;
uint8_t smsResult;

void user1TaskUpdata(void *argument){
	mpu6050AngleData_t angle;
	
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;

		if(user1TaskInfo.user1TaskCnt % 100 == 0){
			DRIVER_GPS_GetPosition(&longti, &lati);
		}

		if(user1TaskInfo.user1TaskCnt % 5 == 0){
			DRIVER_MPU6050_Update();
		}

		/* 100ms 刷新 OLED 三轴角度显示 */
		if(user1TaskInfo.user1TaskCnt % 50 == 0){
			if(DRIVER_MPU6050_GetAngle(&angle) != 0U){
				DRIVER_OLED_ShowString(0U, 0U,  "R:");
				DRIVER_OLED_ShowFloat(12U, 0U,  angle.rollDeg,  1U);
				DRIVER_OLED_ShowString(0U, 16U, "P:");
				DRIVER_OLED_ShowFloat(12U, 16U, angle.pitchDeg, 1U);
				DRIVER_OLED_ShowString(0U, 32U, "Y:");
				DRIVER_OLED_ShowFloat(12U, 32U, angle.yawDeg,   1U);
				DRIVER_OLED_Refresh();
			}
		}

		/* 上电约 10s 后发送一条测试短信（仅触发一次）。
		 * 测试号码与内容可按需修改。
		 */
		if((smsSentFlag == 0U) && (user1TaskInfo.user1TaskCnt == 500U)){
			smsResult = DRIVER_SIM800C_SendSms("13772411182", "SIM800C test OK");
			smsSentFlag = 1U;
			(void)smsResult; /* 调试时可在此处打断点查看 smsResult */
		}

		osDelay(2);
	}
}
