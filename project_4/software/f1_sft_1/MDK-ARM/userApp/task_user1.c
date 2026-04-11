#include "task_user1.h"
#include "driver_ble.h"
#include "driver_oled.h"
#include "driver_senser.h"
#include "driver_tb6612.h"
#include "func_appcom.h"
#include "stdlib_flash.h"

user1TaskInfo_t user1TaskInfo;  /* user1 任务对外公开的运行状态快照 */

/* 任务基础周期与分频配置。 */
#define USER1_TASK_PERIOD_MS             (2U)
#define USER1_PID_PERIOD_TICK            (5U)
#define USER1_APP_PERIOD_TICK            (50U)
#define USER1_TIMER_PERIOD_TICK          (500U)

/* 温控业务参数。 */
#define USER1_TARGET_TEMP_MIN            (35.0f)
#define USER1_TARGET_TEMP_MAX            (48.0f)
#define USER1_OVER_TEMP_LIMIT            (50.0f)
#define USER1_OVER_TEMP_RESUME           (47.0f)

/* 位置式 PID 参数。 */
#define USER1_PID_DT                     (0.01f)
#define USER1_PID_KP                     (10.0f)
#define USER1_PID_KI                     (0.5f)
#define USER1_PID_KD                     (1.0f)
#define USER1_PID_I_MIN                  (-50.0f)
#define USER1_PID_I_MAX                  (50.0f)
#define USER1_OUTPUT_MIN                 (0.0f)
#define USER1_OUTPUT_MAX                 (100.0f)

/* 按摩模式与占空比映射。 */
#define USER1_MASSAGE_MODE_SOFT          (1U)
#define USER1_MASSAGE_MODE_NORMAL        (2U)
#define USER1_MASSAGE_MODE_STRONG        (3U)
#define USER1_MASSAGE_DUTY_SOFT          (40U)
#define USER1_MASSAGE_DUTY_NORMAL        (60U)
#define USER1_MASSAGE_DUTY_STRONG        (80U)

#define USER1_APP_TIMER_MAX_MIN          (60U)

/* 上报给 APP 的状态字节位定义。 */
#define USER1_STATUS_RUNNING_BIT         (1U << 0)
#define USER1_STATUS_HEATER_BIT          (1U << 1)
#define USER1_STATUS_DRY_BURN_BIT        (1U << 2)
#define USER1_STATUS_OVER_TEMP_BIT       (1U << 3)

/* 任务内部辅助状态。 */
static uint8_t  user1HasValidTemp = 0U;      /* currentTemp 是否已有有效温度帧 */
static uint8_t  user1LastAppPower = 0U;      /* 上一次收到的 APP 电源命令 */

static float user1ClampFloat(float value, float minValue, float maxValue);
static uint32_t user1ClampU32(uint32_t value, uint32_t minValue, uint32_t maxValue);
static uint32_t user1MassageModeToDuty(uint8_t mode);
static uint32_t user1HeaterDutyToPulse(uint32_t dutyPercent);
static int16_t user1MotorDutyToSpeed(uint32_t dutyPercent);
static void user1ResetTempPid(void);
static void user1SetHeaterDuty(uint32_t dutyPercent);
static void user1SetMotorDuty(uint32_t dutyPercent);
static void user1ApplyMassageMode(void);
static void user1EnterStandby(void);
static void user1UpdateWaterLevel(void);
static void user1UpdateTemperatureSample(void);
static void user1UpdateFaultCode(void);
static uint8_t user1BuildStatusByte(void);
static void user1UpdateAppReport(void);
static const char *user1GetModeText(uint8_t mode);
static const char *user1GetRunText(uint8_t isRunning);
static const char *user1GetWaterText(uint8_t dryBurnFlag);
static const char *user1GetOverTempText(uint8_t overTempFlag);
static const char *user1GetAlarmText(uint8_t faultCode);
static void user1UpdateOled(void);
static uint8_t user1HandleAppCommand(void);
static void user1HandlePidControl(void);
static void user1HandleTimerTick(void);

/* 浮点限幅。 */
static float user1ClampFloat(float value, float minValue, float maxValue){
	if(value < minValue){
		return minValue;
	}
	if(value > maxValue){
		return maxValue;
	}
	return value;
}

/* 无符号整数限幅。 */
static uint32_t user1ClampU32(uint32_t value, uint32_t minValue, uint32_t maxValue){
	if(value < minValue){
		return minValue;
	}
	if(value > maxValue){
		return maxValue;
	}
	return value;
}

/* 将按摩模式转换成电机占空比百分比。 */
static uint32_t user1MassageModeToDuty(uint8_t mode){
	switch(mode){
		case USER1_MASSAGE_MODE_SOFT:
			return USER1_MASSAGE_DUTY_SOFT;

		case USER1_MASSAGE_MODE_NORMAL:
			return USER1_MASSAGE_DUTY_NORMAL;

		case USER1_MASSAGE_MODE_STRONG:
			return USER1_MASSAGE_DUTY_STRONG;

		default:
			return 0U;
	}
}

/* 将 0~100% 的加热占空比换算成驱动层脉宽值。 */
static uint32_t user1HeaterDutyToPulse(uint32_t dutyPercent){
	uint32_t pulse;  /* 驱动层使用的实际脉宽值 */

	dutyPercent = user1ClampU32(dutyPercent, 0U, 100U);
	pulse = (dutyPercent * SENSER_HEAT_DUTY_MAX) / 100U;
	return pulse;
}

/* 将 0~100% 的电机占空比换算成 TB6612 的速度指令。 */
static int16_t user1MotorDutyToSpeed(uint32_t dutyPercent){
	dutyPercent = user1ClampU32(dutyPercent, 0U, 100U);
	return (int16_t)(dutyPercent * 10U);
}

/* 清空温控 PID 的积分项和上次误差。 */
static void user1ResetTempPid(void){
	user1TaskInfo.pidIntegral = 0.0f;
	user1TaskInfo.pidLastError = 0.0f;
}

/* 同步控制两路加热输出，并更新 heaterOn 状态。 */
static void user1SetHeaterDuty(uint32_t dutyPercent){
	uint32_t pulse;  /* 计算后的加热输出脉宽 */

	dutyPercent = user1ClampU32(dutyPercent, 0U, 100U);
	pulse = user1HeaterDutyToPulse(dutyPercent);

	DRIVER_SENSER_HeatSetDuty(SENSER_HEAT_CH_1, pulse);
	DRIVER_SENSER_HeatSetDuty(SENSER_HEAT_CH_2, pulse);
	user1TaskInfo.heaterOn = (dutyPercent > 0U) ? 1U : 0U;
}

/* 同步控制全部按摩电机，并记录上报用的占空比。 */
static void user1SetMotorDuty(uint32_t dutyPercent){
	uint8_t motorId;      /* 电机遍历索引 */
	int16_t motorSpeed;   /* 转换后的 TB6612 速度命令 */

	dutyPercent = user1ClampU32(dutyPercent, 0U, 100U);
	motorSpeed = user1MotorDutyToSpeed(dutyPercent);

	for(motorId = 0U; motorId < TB6612_MOTOR_COUNT; motorId++){
		DRIVER_TB6612_MotorSetSpeed(motorId, motorSpeed);
	}

	user1TaskInfo.motorPwmDuty = dutyPercent;
}

/* 应用当前目标按摩模式。待机时强制停机，但保留目标模式等待下次开机恢复。 */
static void user1ApplyMassageMode(void){
	uint32_t dutyPercent;  /* 当前目标模式对应的电机占空比 */

	if(user1TaskInfo.sysRunning == 0U){
		user1TaskInfo.massageMode = USER1_MASSAGE_MODE_STOP;
		user1SetMotorDuty(0U);
		return;
	}

	dutyPercent = user1MassageModeToDuty(user1TaskInfo.massageModeTarget);
	user1TaskInfo.massageMode = user1TaskInfo.massageModeTarget;
	user1SetMotorDuty(dutyPercent);
}

/* 进入待机态时，统一关闭加热、电机和定时。 */
static void user1EnterStandby(void){
	user1TaskInfo.sysRunning = 0U;
	user1TaskInfo.timerSetSec = 0U;
	user1TaskInfo.timerRemainSec = 0U;
	user1TaskInfo.timerActive = 0U;
	user1TaskInfo.massageMode = USER1_MASSAGE_MODE_STOP;
	user1ResetTempPid();
	user1SetHeaterDuty(0U);
	user1SetMotorDuty(0U);
}

/* 仅使用水位数组下标 [1] 的通道作为防干烧输入。 */
static void user1UpdateWaterLevel(void){
	uint8_t hasWater;  /* 通道 [1] 的水位检测结果：1=有水，0=无水 */

	hasWater = DRIVER_SENSER_WaterLvlGet(SENSER_WATER_LVL_CH_2);
	user1TaskInfo.dryBurnFlag = (hasWater != 0U) ? 0U : 1U;
}

/* 读取一次温度并处理异常帧、超温状态。 */
static void user1UpdateTemperatureSample(void){
	float tempValue;  /* 本次 DS18B20 读取结果 */

	tempValue = DRIVER_SENSER_DS18B20ReadTemp();
	if(tempValue == SENSER_DS18B20_TEMP_ERROR){
		/* 传感器错误时，本轮温度无效，禁止 PID 使用本次读数。 */
		user1HasValidTemp = 0U;
	} else if(tempValue < 0.0f){
		/* 负数异常帧直接丢弃，保持上一帧有效温度。 */
	} else {
		/* 只有非负有效温度才会覆盖当前温度。 */
		user1HasValidTemp = 1U;
		user1TaskInfo.currentTemp = tempValue;
	}

	/* 超温保护具有最高优先级，恢复时仍需满足已降温且无缺水。 */
	if(user1TaskInfo.currentTemp >= USER1_OVER_TEMP_LIMIT){
		user1TaskInfo.overTempFlag = 1U;
	} else if((user1TaskInfo.currentTemp < USER1_OVER_TEMP_RESUME) && (user1TaskInfo.dryBurnFlag == 0U)){
		user1TaskInfo.overTempFlag = 0U;
	}

	DRIVER_SENSER_DS18B20StartConvert();
}

/* 根据超温和缺水状态合成故障码。 */
static void user1UpdateFaultCode(void){
	if((user1TaskInfo.dryBurnFlag != 0U) && (user1TaskInfo.overTempFlag != 0U)){
		user1TaskInfo.faultCode = 3U;
	} else if(user1TaskInfo.overTempFlag != 0U){
		user1TaskInfo.faultCode = 1U;
	} else if(user1TaskInfo.dryBurnFlag != 0U){
		user1TaskInfo.faultCode = 2U;
	} else {
		user1TaskInfo.faultCode = 0U;
	}
}

/* 组装 TX[7] 的状态字节。 */
static uint8_t user1BuildStatusByte(void){
	uint8_t statusByte;  /* 上报给 APP 的运行状态字节 */

	statusByte = 0U;
	if(user1TaskInfo.sysRunning != 0U){
		statusByte |= USER1_STATUS_RUNNING_BIT;
	}
	if(user1TaskInfo.heaterOn != 0U){
		statusByte |= USER1_STATUS_HEATER_BIT;
	}
	if(user1TaskInfo.dryBurnFlag != 0U){
		statusByte |= USER1_STATUS_DRY_BURN_BIT;
	}
	if(user1TaskInfo.overTempFlag != 0U){
		statusByte |= USER1_STATUS_OVER_TEMP_BIT;
	}

	return statusByte;
}

/* 将当前任务状态写回 APP 变量池。 */
static void user1UpdateAppReport(void){
	remoteInfo.remoteVar_TX[4].var_float = user1TaskInfo.currentTemp;
	remoteInfo.remoteVar_TX[5].var_uint32 = (uint32_t)user1TaskInfo.targetTemp;
	remoteInfo.remoteVar_TX[6].var_uint32 = (uint32_t)user1TaskInfo.massageMode;
	remoteInfo.remoteVar_TX[7].var_uint32 = (uint32_t)user1BuildStatusByte();
	remoteInfo.remoteVar_TX[8].var_uint32 = user1TaskInfo.timerRemainSec;
	remoteInfo.remoteVar_TX[9].var_uint32 = user1TaskInfo.motorPwmDuty;
	remoteInfo.remoteVar_TX[10].var_uint32 = (uint32_t)user1TaskInfo.faultCode;
}

static const char *user1GetModeText(uint8_t mode){
	switch(mode){
		case USER1_MASSAGE_MODE_SOFT:
			return "SOFT";

		case USER1_MASSAGE_MODE_NORMAL:
			return "NORM";

		case USER1_MASSAGE_MODE_STRONG:
			return "STRG";

		default:
			return "STOP";
	}
}

static const char *user1GetRunText(uint8_t isRunning){
	if(isRunning != 0U){
		return "RUN";
	}
	return "IDLE";
}

static const char *user1GetWaterText(uint8_t dryBurnFlag){
	if(dryBurnFlag != 0U){
		return "LOW";
	}
	return "OK";
}

static const char *user1GetOverTempText(uint8_t overTempFlag){
	if(overTempFlag != 0U){
		return "HOT";
	}
	return "OK";
}

static const char *user1GetAlarmText(uint8_t faultCode){
	switch(faultCode){
		case 1U:
			return "OVER";

		case 2U:
			return "DRY";

		case 3U:
			return "BOTH";

		default:
			return "NONE";
	}
}

/* OLED 仅作本地监控显示，不参与控制决策。 */
static void user1UpdateOled(void){
	uint8_t xNext;         /* 当前一行文本的下一个显示起点 */
	uint32_t systemTimeSec; /* 系统累计运行时间，单位：秒 */

	DRIVER_OLED_Clear();
	systemTimeSec = user1TaskInfo.taskCnt / USER1_TIMER_PERIOD_TICK;

	DRIVER_OLED_ShowString(0U, 0U, "TEMP:");
	if(user1HasValidTemp != 0U){
		xNext = DRIVER_OLED_ShowFloat(30U, 0U, user1TaskInfo.currentTemp, 1U);
		DRIVER_OLED_ShowChar6x8(xNext, 0U, 'C', 1U);
	} else {
		DRIVER_OLED_ShowString(30U, 0U, "ERR");
	}

	DRIVER_OLED_ShowString(0U, 8U, "TGT:");
	xNext = DRIVER_OLED_ShowFloat(24U, 8U, user1TaskInfo.targetTemp, 0U);
	DRIVER_OLED_ShowChar6x8(xNext, 8U, 'C', 1U);

	DRIVER_OLED_ShowString(0U, 16U, "SYS:");
	DRIVER_OLED_ShowString(24U, 16U, user1GetRunText(user1TaskInfo.sysRunning));
	DRIVER_OLED_ShowString(66U, 16U, "H:");
	DRIVER_OLED_ShowString(78U, 16U, (user1TaskInfo.heaterOn != 0U) ? "ON" : "OFF");

	DRIVER_OLED_ShowString(0U, 24U, "MODE:");
	DRIVER_OLED_ShowString(30U, 24U, user1GetModeText(user1TaskInfo.massageMode));

	DRIVER_OLED_ShowString(0U, 32U, "TIME:");
	DRIVER_OLED_ShowNum(30U, 32U, user1TaskInfo.timerRemainSec, 4U);
	DRIVER_OLED_ShowChar6x8(54U, 32U, 'S', 1U);

	DRIVER_OLED_ShowString(0U, 40U, "MPWM:");
	DRIVER_OLED_ShowNum(30U, 40U, user1TaskInfo.motorPwmDuty, 3U);
	DRIVER_OLED_ShowChar6x8(48U, 40U, '%', 1U);
	DRIVER_OLED_ShowString(66U, 40U, "FLT:");
	DRIVER_OLED_ShowNum(90U, 40U, user1TaskInfo.faultCode, 1U);

	DRIVER_OLED_ShowString(0U, 48U, "W:");
	DRIVER_OLED_ShowString(12U, 48U, user1GetWaterText(user1TaskInfo.dryBurnFlag));
	DRIVER_OLED_ShowString(54U, 48U, "O:");
	DRIVER_OLED_ShowString(66U, 48U, user1GetOverTempText(user1TaskInfo.overTempFlag));

	DRIVER_OLED_ShowString(0U, 56U, "SEC:");
	DRIVER_OLED_ShowString(24U, 56U, user1GetAlarmText(user1TaskInfo.faultCode));
	DRIVER_OLED_ShowNum(24U, 56U, systemTimeSec, 5U);


	DRIVER_OLED_Refresh();
}

/*
 * 处理 APP 下发的控制命令。
 * 约定：APP 指令优先级高于同周期本地控制，因此主循环会先调用本函数，再执行 PID。
 * 返回值 skipTimerTick=1 表示本周期刚收到定时或开关状态变更，1s 定时器本周期不应再扣减。
 */
static uint8_t user1HandleAppCommand(void){
	uint32_t appTarget;      /* APP 下发的目标温度 */
	uint32_t appTimer;       /* APP 下发的定时分钟 */
	uint8_t appMode;         /* APP 下发的按摩模式 */
	uint8_t appPower;        /* APP 下发的电源状态 */
	uint8_t skipTimerTick;   /* 本周期是否跳过 1s 定时扣减 */
	uint32_t newTimerSetSec; /* 按 APP 当前分钟值换算出的目标总秒数 */

	appTarget = remoteInfo.remoteVar_RX[0].var_uint32;
	appTimer = remoteInfo.remoteVar_RX[1].var_uint32;
	/* APPCOM 当前把 0x13 帧第 1 个 1B 字段提取到 systemEnable，userTask 按新的模式入口读取。 */
	appMode = remoteInfo.remoteVar_RX[2].var_uint32;
	appPower = (uint8_t)(remoteInfo.remoteVar_RX[3].var_uint32 & 0xFFU);
	skipTimerTick = 0U;

	/* APP 下发的目标温度始终优先采用，但仍受安全范围限制。 */
	if((appTarget >= (uint32_t)USER1_TARGET_TEMP_MIN) && (appTarget <= (uint32_t)USER1_TARGET_TEMP_MAX)){
		user1TaskInfo.targetTemp = (float)appTarget;
	}

	/* APP 下发的模式命令直接覆盖目标模式。 */
	if(appMode <= USER1_MASSAGE_MODE_STRONG){
		user1TaskInfo.massageModeTarget = appMode;
	}

	/* APP 调整定时后，新的剩余时间立即生效。 */
	appTimer = user1ClampU32(appTimer, 0U, USER1_APP_TIMER_MAX_MIN);
	newTimerSetSec = appTimer * 60U;
	if(appTimer == 0U){
		if((user1TaskInfo.timerSetSec != 0U)
			|| (user1TaskInfo.timerRemainSec != 0U)
			|| (user1TaskInfo.timerActive != 0U)){
			user1TaskInfo.timerSetSec = 0U;
			user1TaskInfo.timerRemainSec = 0U;
			user1TaskInfo.timerActive = 0U;
			skipTimerTick = 1U;
		}
	} else if((user1TaskInfo.timerSetSec != newTimerSetSec)
		|| (user1TaskInfo.timerActive == 0U)){
		/*
		 * 以当前已生效的本地定时配置为准判断是否需要刷新。
		 * 这样 APP 修改分钟值时会立即重装剩余时间；即使待机后再次下发同一时长，也能重新启动倒计时。
		 */
		user1TaskInfo.timerSetSec = newTimerSetSec;
		user1TaskInfo.timerRemainSec = newTimerSetSec;
		user1TaskInfo.timerActive = 1U;
		skipTimerTick = 1U;
	}

	/* 开关机命令优先级高于本地控制，同周期立即执行。 */
	appPower = (appPower != 0U) ? 1U : 0U;
	if(appPower != user1LastAppPower){
		if(appPower != 0U){
			user1TaskInfo.sysRunning = 1U;
			user1ApplyMassageMode();
		} else {
			user1EnterStandby();
		}
		user1LastAppPower = appPower;
		skipTimerTick = 1U;
	}

	/* 运行态应用 APP 目标模式，待机态则仅保存目标模式。 */
	if(user1TaskInfo.sysRunning != 0U){
		user1ApplyMassageMode();
	} else {
		user1TaskInfo.massageMode = USER1_MASSAGE_MODE_STOP;
		user1SetMotorDuty(0U);
	}

	return skipTimerTick;
}

/*
 * 本地温控闭环。
 * 注意：heaterOn 是“实际加热输出状态”，不是 APP 直接设置值；
 * APP 通过目标温度和电源状态影响它，而超温/缺水保护可以随时强制关闭加热。
 */
static void user1HandlePidControl(void){
	float error;           /* 当前温度误差 */
	float derivative;      /* 误差变化率 */
	float output;          /* PID 计算输出，占空比百分比 */
	uint32_t dutyPercent;  /* 四舍五入后的加热占空比 */

	/* 运行条件不满足时，本地控制立即让位给安全保护或待机状态。 */
	if((user1TaskInfo.sysRunning == 0U)
		|| (user1TaskInfo.overTempFlag != 0U)
		|| (user1TaskInfo.dryBurnFlag != 0U)
		|| (user1HasValidTemp == 0U)){
		user1ResetTempPid();
		user1SetHeaterDuty(0U);
		return;
	}

	/* 标准位置式 PID 计算。 */
	error = user1TaskInfo.targetTemp - user1TaskInfo.currentTemp;
	user1TaskInfo.pidIntegral += (error * USER1_PID_DT);
	user1TaskInfo.pidIntegral = user1ClampFloat(user1TaskInfo.pidIntegral, USER1_PID_I_MIN, USER1_PID_I_MAX);
	derivative = (error - user1TaskInfo.pidLastError) / USER1_PID_DT;
	output = (USER1_PID_KP * error)
		+ (USER1_PID_KI * user1TaskInfo.pidIntegral)
		+ (USER1_PID_KD * derivative);
	output = user1ClampFloat(output, USER1_OUTPUT_MIN, USER1_OUTPUT_MAX);
	user1TaskInfo.pidLastError = error;

	dutyPercent = (uint32_t)(output + 0.5f);
	user1SetHeaterDuty(dutyPercent);
}

/* 运行态下的 1s 定时倒计时。 */
static void user1HandleTimerTick(void){
	if((user1TaskInfo.timerActive == 0U) || (user1TaskInfo.sysRunning == 0U)){
		return;
	}

	if(user1TaskInfo.timerRemainSec > 0U){
		user1TaskInfo.timerRemainSec--;
	}

	if(user1TaskInfo.timerRemainSec == 0U){
		user1TaskInfo.timerActive = 0U;
		user1TaskInfo.timerSetSec = 0U;
		user1EnterStandby();
	}
}

/* 任务初始化：只做本任务私有资源和状态初始化，不修改系统任务。 */
void user1TaskInit(void){
	DRIVER_BLE_Init();
	DRIVER_OLED_Init();
	DRIVER_SENSER_Init();
	DRIVER_SENSER_DS18B20Init();
	/* user1TaskInfo 已由 systemTaskInit 完成 Flash 恢复，此处不再重复初始化。 */

	user1HasValidTemp = 0U;
	/* 初始化为 flash 恢复的运行状态，防止 APP 重连时因 lastAppPower=0 与下发值不同而误触发状态切换。 */
	user1LastAppPower = user1TaskInfo.sysRunning;

	user1SetHeaterDuty(0U);
	user1SetMotorDuty(0U);
	DRIVER_SENSER_WaterLvlUpdate();
	user1UpdateWaterLevel();
	user1UpdateFaultCode();
	user1UpdateAppReport();
	user1UpdateOled();
	DRIVER_SENSER_DS18B20StartConvert();
}

void user1TaskUpdata(void *argument){
	uint8_t needReport;      /* 本轮是否需要刷新 APP 上报和 OLED */
	uint8_t skipTimerTick;   /* 本轮是否跳过定时扣减 */

	(void)argument;

	/* 等系统任务先完成底层外设初始化，再启动业务逻辑。 */
	osDelay(20U);
	user1TaskInit();

	for(;;){
		/* taskCnt 作为所有子周期调度的统一时基。 */
		user1TaskInfo.taskCnt++;
		needReport = 0U;
		skipTimerTick = 0U;

		/* 2ms 周期：先更新水位，确保保护状态始终最新。 */
		DRIVER_SENSER_WaterLvlUpdate();
		user1UpdateWaterLevel();

		/*
		 * 100ms 周期：先处理 APP 命令。
		 * 这样同一调度周期里，APP 最新指令会先落地，再交给本地 PID 执行，实现“APP 优先”。
		 */
		if((user1TaskInfo.taskCnt % USER1_APP_PERIOD_TICK) == 0U){
			user1UpdateTemperatureSample();
			skipTimerTick = user1HandleAppCommand();
			user1UpdateFaultCode();
			needReport = 1U;
		}

		/* 1s 周期：没有刚被 APP 重置定时时才继续扣减。 */
		if((user1TaskInfo.taskCnt % USER1_TIMER_PERIOD_TICK) == 0U){
			if(skipTimerTick == 0U){
				user1HandleTimerTick();
			}
			user1UpdateFaultCode();
			needReport = 1U;
		}

		/* 10ms 周期：APP 命令处理完成后，再执行本地 PID。 */
		if((user1TaskInfo.taskCnt % USER1_PID_PERIOD_TICK) == 0U){
			user1HandlePidControl();
		}

		/* 上报和 OLED 刷新只在较慢周期执行，避免无意义重复刷新。 */
		if(needReport != 0U){
			user1UpdateAppReport();
			user1UpdateOled();
		}

		osDelay(USER1_TASK_PERIOD_MS);
	}
}
