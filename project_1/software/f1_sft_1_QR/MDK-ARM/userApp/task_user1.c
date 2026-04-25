#include "task_user1.h"
#include "task_system.h"
#include "driver_oled.h"
#include "driver_tb6612.h"
#include "driver_board.h"
#include "driver_steer.h"
#include "driver_qr.h"
#include "func_appcom.h"

/*============================================================================
 * 用户配置：obj1编码到OLED显示标签的映射表
 * 每条记录格式：{ 编码值, "显示字符串（最多5字符）" }
 * 0xFF（无目标/冷却中）固定显示"NONE "，无需在此配置
 *============================================================================*/
typedef struct {
  uint8_t     code;   /* obj1编码值 */
  const char *label;  /* OLED显示字符串，最多5字符，不足时用空格补齐 */
} obj1LabelMap_t;

static const obj1LabelMap_t obj1LabelMap[] = {
  { 0x31U, "AIM_1" },   /* 目的地A */
  { 0x32U, "AIM_2" },   /* 目的地B */
	{ 0x33U, "AIM_3" },   /* 目的地B */
  /* 在此继续添加映射，格式：{ 编码值, "标签 " } */
};

#define OBJ1_LABEL_MAP_COUNT  (sizeof(obj1LabelMap) / sizeof(obj1LabelMap[0]))

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 传送带延迟参数（单位：user1任务循环次数，每次2ms） */
uint32_t timeA = 3500U;
uint32_t timeB = 1400U;  

/* 包裹列表 */
packInfo_t packList[PACK_MAX_NUM];

/* 最近一次识别到的包裹信息快照 */
static packInfo_t packLatestInfo;

/* 系统运行状态 */
static sysCtrl_t sysCtrl;

/* 传送带电机当前速度（用于软启动斜坡控制） */
static int16_t motorSpeed;

/* 最近一次有效的QR数据值，初始化为0xFF（未收到数据） */
static uint8_t lastValidObj1 = 0xFFU;

/* QR模块有效数据包累计计数 */
static uint32_t qrLocalPktCnt = 0U;

user1TaskInfo_t user1TaskInfo;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 查询obj1编码对应的显示标签；0xFF返回"NONE "，表中未找到返回"UNKN " */
static const char* __getObj1Label(uint8_t obj1){
  uint8_t i;

  if(obj1 == 0xFFU){
    return "NONE ";
  }
  for(i = 0U; i < OBJ1_LABEL_MAP_COUNT; i++){
    if(obj1LabelMap[i].code == obj1){
      return obj1LabelMap[i].label;
    }
  }
  return "UNKN ";
}

/* 检测QR新数据，将包裹信息加入列表并计算舵机触发时刻
 * 仅当qrInfo.hasNewData置位时才处理，读取后清除标志防止重复处理 */
static void packRecognize(void){
  if(qrInfo.hasNewData == 0U){
    return;
  }
  qrInfo.hasNewData = 0U;

  packList[sysCtrl.packNum].aim       = qrInfo.data1;
  packList[sysCtrl.packNum].isOutFlag = 0U;

  lastValidObj1 = qrInfo.data1;

  switch(qrInfo.data1){
  case 0x31U:  /* 目的地A：延迟timeA个循环后触发舵机1 */
    packList[sysCtrl.packNum].outportTime = sysCtrl.taskCnt + CONVEYOR_DELAY_A;
    packList[sysCtrl.packNum].servoId     = STEER_SERVO_1;
    break;
  case 0x32U:  /* 目的地B：延迟timeB个循环后触发舵机2 */
    packList[sysCtrl.packNum].outportTime = sysCtrl.taskCnt + CONVEYOR_DELAY_B;
    packList[sysCtrl.packNum].servoId     = STEER_SERVO_2;
    break;
  default:     /* 目的地C：不触发舵机，直接通过 */
    packList[sysCtrl.packNum].outportTime = 0U;
    packList[sysCtrl.packNum].servoId     = SERVO_ID_NONE;
    break;
  }

  qrLocalPktCnt++;
  packLatestInfo.aim         = packList[sysCtrl.packNum].aim;
  packLatestInfo.packNum     = (uint8_t)(qrLocalPktCnt & 0xFFU);
  packLatestInfo.outportTime = packList[sysCtrl.packNum].outportTime;
  packLatestInfo.servoId     = packList[sysCtrl.packNum].servoId;

  sysCtrl.packNum++;
  if(sysCtrl.packNum >= PACK_MAX_NUM){
    sysCtrl.packNum = 0U;
  }
}

/* 遍历包裹列表，到达触发时刻时使能对应舵机
 * 用有符号减法比较避免taskCnt溢出时漏触发 */
static void sysControl(void){
  uint8_t i;

  for(i = 0U; i < sysCtrl.packNum; i++){
    if(packList[i].isOutFlag == 0U &&
       packList[i].servoId   != SERVO_ID_NONE &&
       (int32_t)(sysCtrl.taskCnt - packList[i].outportTime) >= 0){

      if(packList[i].servoId == STEER_SERVO_1){
        sysCtrl.servoEnable[STEER_SERVO_1] = 1U;
      } else if(packList[i].servoId == STEER_SERVO_2){
        sysCtrl.servoEnable[STEER_SERVO_2] = 1U;
      }
      packList[i].isOutFlag = 1U;
    }
  }
}

/* 传送带电机控制：斜坡加速至最大速度 */
static void motorControl(void){
  if(motorSpeed < TB6612_SPEED_MAX){
    motorSpeed += MOTOR_SPEED_RAMP_STEP;
  } else {
    motorSpeed = (int16_t)TB6612_SPEED_MAX;
  }
  DRIVER_TB6612_MotorSetSpeed(TB6612_MOTOR_A, -motorSpeed);
}

/* 舵机控制：仅在KEY1按下时触发，顺时针拨开后逆时针回位
 * 使用阻塞式Rotate360，执行期间任务挂起 */
static void servoControl(void){
  uint8_t i;

//  if(!boardInfo.key[BOARD_KEY1].isPressed){
//    return;
//  }
	
	
  for(i = 0U; i < STEER_SERVO_COUNT; i++){
    if(sysCtrl.servoEnable[i] == 1U){
      if(i == STEER_SERVO_1){
				DRIVER_STEER_Rotate360(STEER_SERVO_1, STEER_DIR_CCW,  400U);
				DRIVER_STEER_Rotate360(STEER_SERVO_1, STEER_DIR_CW, 390U);

      } else {
        DRIVER_STEER_Rotate360(STEER_SERVO_2, STEER_DIR_CCW, 400U);
        DRIVER_STEER_Rotate360(STEER_SERVO_2, STEER_DIR_CW,  390U);
      }
      sysCtrl.servoEnable[i] = 0U;
    }
  }
}

/* OLED显示刷新：先将上一帧推送到屏幕，再更新显存
 * 显示内容：系统时间、系统使能、摄像头连接状态、fps、obj1值、有效帧数 */
static void oledControl(void){
  /* 先将上一帧写入屏幕，再开始新一帧绘制（减少I2C阻塞对控制循环的影响） */
  DRIVER_OLED_Refresh();

  /* 系统时间：systemTaskCnt为10ms/tick，/100=整秒，(/10)%10=十分之一秒 */
  DRIVER_OLED_ShowString(5,  0,  "time:");
  DRIVER_OLED_ShowNum(35, 0,  systemTaskInfo.systemTaskCnt / 100U, 4);
  DRIVER_OLED_DrawPoint(60, 5,  1U);
  DRIVER_OLED_ShowNum(63, 0,  (systemTaskInfo.systemTaskCnt / 10U) % 10U, 1);
  DRIVER_OLED_ShowString(72, 0,  "s");

  /* 系统使能状态 */
  if(systemTaskInfo.systemEnable_board){
    DRIVER_OLED_ShowString(5,  11, "SYS: ENABLE ");
  } else {
    DRIVER_OLED_ShowString(5,  11, "SYS: DISABLE");
  }

  /* QR连接状态：收到至少一帧数据即视为已连接 */
  if(qrInfo.rxTotalCnt > 0U){
    DRIVER_OLED_ShowString(5,  22, "QR: CONNECTED  ");
  } else {
    DRIVER_OLED_ShowString(5,  22, "QR: NO CONN    ");
  }

  /* 累计接收帧数 */
  DRIVER_OLED_ShowString(5,  33, "RX:  ");
  DRIVER_OLED_ShowNum(35, 33, qrInfo.rxTotalCnt, 5);

  /* QR数据：显示最近一次收到的数据值 */
  DRIVER_OLED_ShowString(5,  44, "DATA:");
  DRIVER_OLED_ShowString(35, 44, __getObj1Label(lastValidObj1));

  /* 有效数据包计数（累计） */
  DRIVER_OLED_ShowString(5,  55, "CNT: ");
  DRIVER_OLED_ShowNum(35, 55, qrLocalPktCnt, 5);
}

/*============================================================================
 * 任务入口
 *============================================================================*/

void user1TaskInit(){
  DRIVER_QR_Init();
  /* 初始化360度舵机为停止状态  STEER_DIR_CCW 为外推*/

  /* 等待OLED VCC上电稳定（SSD1306要求VCC稳定后≥100ms才可接受I2C命令） */
  osDelay(100);
  DRIVER_OLED_Init();
	

	DRIVER_STEER_Rotate360(STEER_SERVO_1, STEER_DIR_CCW,  95U);
  DRIVER_STEER_Rotate360(STEER_SERVO_2, STEER_DIR_CCW,  95U);
	

}

void user1TaskUpdata(void *argument){
  user1TaskInit();
  for(;;){
    user1TaskInfo.user1TaskCnt++;
    sysCtrl.taskCnt++;

    DRIVER_STEER_Updata();

		if(systemTaskInfo.systemEnable_board ){
			sysControl();
			motorControl();
			servoControl();
		}else{
			DRIVER_TB6612_MotorSetSpeed(TB6612_MOTOR_A, 0);
		}

    if(user1TaskInfo.user1TaskCnt % 5U == 0U){
      /* 10ms TASK */
      packRecognize();
    }

    if(user1TaskInfo.user1TaskCnt % 50U == 0U){
      /* 100ms TASK：OLED刷新 */
      oledControl();
    }

    osDelay(2);
  }
}
