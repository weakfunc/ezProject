#include "task_user1.h"
#include "func_appcom.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_ble.h"
#include "driver_gps.h"
#include "driver_mpu6050.h"
#include "stdlib_sleep.h"
#include <math.h>

/*============================================================================
 * 内部配置（仅task_user1模块内部使用）
 *============================================================================*/

#define USER1_SLEEP_TIMEOUT_MS              (30000U)
#define USER1_BASE_PERIOD_MS                (2U)
#define ANTITHEFT_DISPLACE_THRESHOLD_M      (10.0f)
#define VIBRATION_THRESHOLD                 (0.3f)
#define VIBRATION_CONFIRM_CNT               (3U)
#define USER1_ALARM_SOURCE_NONE             (0U)
#define USER1_ALARM_SOURCE_VIBRATION        (1U)
#define USER1_ALARM_SOURCE_DISPLACE         (2U)
#define USER1_APP_CMD_NONE                  (0xFFU)
#define USER1_BUZZ_CONTINUOUS_MS            (0xFFFFU)
#define USER1_EARTH_RADIUS_M                (6371000.0f)
#define USER1_PI                            (3.14159265f)

/* GPS上报帧序号 */
static uint32_t user1GpsFrameSeq = 0U;
/* 震动确认计数 */
static uint8_t user1VibrationCnt = 0U;
/* 声光报警输出翻转状态 */
static uint8_t user1AlarmOutputOn = 0U;
/* KEY1清警按键上次状态，用于边沿检测 */
static uint8_t user1Key1LastPressed = 0U;

user1TaskInfo_t user1TaskInfo;

/* 计算两点间近似直线距离，返回单位为米 */
static float calcDistanceM(float lat1, float lon1, float lat2, float lon2){
  float dlat = (lat2 - lat1) * USER1_PI / 180.0f;
  float dlon = (lon2 - lon1) * USER1_PI / 180.0f;
  float meanLat = (lat1 + lat2) * 0.5f * USER1_PI / 180.0f;
  float dx = dlon * USER1_EARTH_RADIUS_M * cosf(meanLat);
  float dy = dlat * USER1_EARTH_RADIUS_M;
  return sqrtf(dx * dx + dy * dy);
}

/* 触发指定来源的报警，并确保系统退出休眠态 */
static void user1TaskSetAlarm(uint8_t alarmSource){
  if(user1TaskInfo.alarmFlag == 0U){
    user1TaskInfo.alarmSource = alarmSource;
  }

  user1TaskInfo.alarmFlag = 1U;
  user1TaskInfo.isSleeping = 0U;
  user1TaskInfo.sleepCountdownMs = USER1_SLEEP_TIMEOUT_MS;

  if(oledInfo.isOn == 0U){
    DRIVER_OLED_DisplayOn();
  }
}

/* 位移报警清除后，将当前位置重新记录为参考位置 */
static void user1TaskResetDisplaceReference(uint8_t clearedAlarmSource){
  if(clearedAlarmSource != USER1_ALARM_SOURCE_DISPLACE){
    return;
  }

  if(user1TaskInfo.gpsValid != 0U){
    user1TaskInfo.refLatitude = user1TaskInfo.gpsLatitude;
    user1TaskInfo.refLongitude = user1TaskInfo.gpsLongitude;
    user1TaskInfo.refPositionSet = 1U;
  } else {
    user1TaskInfo.refPositionSet = 0U;
  }
}

/* 清除报警并停止声光输出 */
static void user1TaskClearAlarm(uint8_t resetDisplaceRef){
  uint8_t clearedAlarmSource = user1TaskInfo.alarmSource;

  if(resetDisplaceRef != 0U){
    user1TaskResetDisplaceReference(clearedAlarmSource);
  }

  user1TaskInfo.alarmFlag = 0U;
  user1TaskInfo.alarmSource = USER1_ALARM_SOURCE_NONE;
  user1VibrationCnt = 0U;
  user1AlarmOutputOn = 0U;
  boardInfo.buzzTimeMs = 0U;
  DRIVER_BOARD_RgbSet(BOARD_RGB_R, 0U);
  DRIVER_BOARD_RgbSet(BOARD_RGB_G, 1U);
}

/* 判断任意板载按键是否处于按下状态 */
static uint8_t user1TaskIsAnyKeyPressed(void){
  if(DRIVER_BOARD_KeyIsPressed(BOARD_KEY1) != 0U) return 1U;
  if(DRIVER_BOARD_KeyIsPressed(BOARD_KEY2) != 0U) return 1U;
  if(DRIVER_BOARD_KeyIsPressed(BOARD_KEY3) != 0U) return 1U;
  return 0U;
}

/* 复位休眠倒计时并退出休眠态 */
static void user1TaskWakeUp(void){
  user1TaskInfo.isSleeping = 0U;
  user1TaskInfo.sleepCountdownMs = USER1_SLEEP_TIMEOUT_MS;
  STDLIB_SLEEP_WakeUp();

  if(oledInfo.isOn == 0U){
    DRIVER_OLED_DisplayOn();
  }
}

/* KEY1作为STM32端清除报警按钮 */
static void user1TaskLocalClearKeyProcess(void){
  uint8_t key1Pressed = DRIVER_BOARD_KeyIsPressed(BOARD_KEY1);

  if((key1Pressed != 0U) && (user1Key1LastPressed == 0U)){
    if(user1TaskInfo.alarmFlag != 0U){
      user1TaskClearAlarm(1U);
    }
    user1TaskWakeUp();
  }

  user1Key1LastPressed = key1Pressed;
}

/* 进入休眠显示状态，并调用低功耗接口 */
static void user1TaskEnterSleep(void){
  user1TaskInfo.isSleeping = 1U;

  if(oledInfo.isOn != 0U){
    DRIVER_OLED_Clear();
    DRIVER_OLED_Refresh();
    DRIVER_OLED_DisplayOff();
  }

  STDLIB_SLEEP_EnterSleep();
}

/* 2ms休眠倒计时与按键唤醒处理 */
static void user1TaskSleepWakeProcess(void){
  user1TaskLocalClearKeyProcess();

  if(user1TaskIsAnyKeyPressed() != 0U){
    user1TaskWakeUp();
    return;
  }

  if(user1TaskInfo.alarmFlag != 0U){
    user1TaskWakeUp();
    return;
  }

  if(user1TaskInfo.isSleeping != 0U){
    STDLIB_SLEEP_EnterSleep();
    return;
  }

  if(user1TaskInfo.sleepCountdownMs > USER1_BASE_PERIOD_MS){
    user1TaskInfo.sleepCountdownMs -= USER1_BASE_PERIOD_MS;
  } else {
    user1TaskInfo.sleepCountdownMs = 0U;
    user1TaskEnterSleep();
  }
}

/* 处理APP远程防盗开关与报警清除指令 */
static void user1TaskAppRxProcess(void){
  uint8_t appAntitheftCmd;
  uint8_t appAlarmClearCmd;

  /* 当前func_appcom将APP的RX[2]固定映射到systemEnable字段 */
  appAntitheftCmd = (uint8_t)(remoteInfo.systemEnable & 0x01U);
  appAlarmClearCmd = (uint8_t)(remoteInfo.remoteVar_RX[3].var_uint32 & 0xFFU);

  user1TaskInfo.appAntitheftCmd = appAntitheftCmd;
  user1TaskInfo.appAlarmClearCmd = appAlarmClearCmd;

  if((appAntitheftCmd == 0U) || (appAntitheftCmd == 1U)){
    if(appAntitheftCmd != user1TaskInfo.antitheftEnabled){
      user1TaskInfo.antitheftEnabled = appAntitheftCmd;
      user1TaskWakeUp();

      if(appAntitheftCmd == 0U){
        user1TaskClearAlarm(0U);
        user1TaskInfo.refPositionSet = 0U;
      } else {
        user1TaskInfo.refPositionSet = 0U;
      }
    }
  }

  if(appAlarmClearCmd == 1U){
    user1TaskClearAlarm(1U);
    user1TaskWakeUp();
  }
}

/* 100ms震动检测防盗 */
static void user1TaskVibrationDetect(void){
  mpu6050AccelData_t accel;
  float accTotal;
  float accDiff;

  if(user1TaskInfo.antitheftEnabled == 0U){
    user1VibrationCnt = 0U;
    return;
  }

  if(DRIVER_MPU6050_GetAccel(&accel) == 0U){
    return;
  }

  accTotal = sqrtf(accel.xG * accel.xG + accel.yG * accel.yG + accel.zG * accel.zG);
  accDiff = accTotal - 1.0f;
  if(accDiff < 0.0f){
    accDiff = -accDiff;
  }

  if(accDiff > VIBRATION_THRESHOLD){
    if(user1VibrationCnt < VIBRATION_CONFIRM_CNT){
      user1VibrationCnt++;
    }

    if(user1VibrationCnt >= VIBRATION_CONFIRM_CNT){
      user1TaskSetAlarm(USER1_ALARM_SOURCE_VIBRATION);
      user1VibrationCnt = 0U;
    }
  } else {
    if(user1VibrationCnt > 0U){
      user1VibrationCnt--;
    }
  }
}

/* 100ms声光报警控制 */
static void user1TaskAlarmOutputProcess(void){
  if(user1TaskInfo.alarmFlag != 0U){
    user1AlarmOutputOn = (user1AlarmOutputOn == 0U) ? 1U : 0U;
    boardInfo.buzzTimeMs = (user1AlarmOutputOn != 0U) ? USER1_BUZZ_CONTINUOUS_MS : 0U;
    DRIVER_BOARD_RgbSet(BOARD_RGB_G, user1AlarmOutputOn);
    DRIVER_BOARD_RgbSet(BOARD_RGB_R, user1AlarmOutputOn);
    return;
  }

  user1AlarmOutputOn = 0U;
  boardInfo.buzzTimeMs = 0U;
  DRIVER_BOARD_RgbSet(BOARD_RGB_R, 0U);
  DRIVER_BOARD_RgbSet(BOARD_RGB_G, 1U);
}

/* 将GPS状态枚举转换为OLED显示字符串 */
static const char *user1TaskGpsStatusText(uint8_t gpsStatus){
  switch((gpsStatus_t)gpsStatus){
    case GPS_STATUS_DISCONNECTED:
      return "GPS: DISCONNECTED";

    case GPS_STATUS_SEARCHING:
      return "GPS: SEARCHING";

    case GPS_STATUS_WORKING:
      return "GPS: WORKING";

    default:
      return "GPS: UNKNOWN";
  }
}

/* 将报警来源转换为OLED显示字符串 */
static const char *user1TaskAlarmSourceText(uint8_t alarmSource){
  switch(alarmSource){
    case USER1_ALARM_SOURCE_NONE:
      return "NONE";

    case USER1_ALARM_SOURCE_VIBRATION:
      return "VIBRATION";

    case USER1_ALARM_SOURCE_DISPLACE:
      return "OFFSET";

    default:
      return "UNKNOWN";
  }
}

/* 100ms写入APP状态类TX变量 */
static void user1TaskAppTxStatusWrite(void){
  uint8_t sysStatus = 0U;

  if(user1TaskInfo.isSleeping != 0U){
    sysStatus = 1U;
  } else if(user1TaskInfo.alarmFlag != 0U){
    sysStatus = 2U;
  }

  remoteInfo.remoteVar_TX[7].var_uint32 = (uint32_t)user1TaskInfo.alarmFlag;
  remoteInfo.remoteVar_TX[10].var_uint32 = (uint32_t)user1TaskInfo.antitheftEnabled;
  remoteInfo.remoteVar_TX[11].var_uint32 = (uint32_t)sysStatus;
}

/* 1000ms读取GPS数据并写入APP GPS相关TX变量 */
static void user1TaskGpsUpdateAndTxWrite(void){
  gpsInfo_t gps;

  if(DRIVER_GPS_GetInfo(&gps) != 0U){
    user1TaskInfo.gpsLatitude = gps.latitudeDeg;
    user1TaskInfo.gpsLongitude = gps.longitudeDeg;
    user1TaskInfo.gpsValid = ((gps.fixValid != 0U) && (gps.hasLocation != 0U)) ? 1U : 0U;
    user1TaskInfo.gpsStatus = (uint8_t)gps.status;
  } else {
    user1TaskInfo.gpsStatus = (uint8_t)gpsInfo.status;
  }

  remoteInfo.remoteVar_TX[4].var_float = user1TaskInfo.gpsLatitude;
  remoteInfo.remoteVar_TX[5].var_float = user1TaskInfo.gpsLongitude;
  remoteInfo.remoteVar_TX[6].var_uint32 = (uint32_t)user1TaskInfo.gpsValid;
  remoteInfo.remoteVar_TX[8].var_uint32 = user1GpsFrameSeq++;
}

/* 1000ms GPS位移防盗检测 */
static void user1TaskDisplaceDetect(void){
  float distanceM;

  if(user1TaskInfo.antitheftEnabled == 0U){
    user1TaskInfo.refPositionSet = 0U;
    return;
  }

  if(user1TaskInfo.gpsValid == 0U){
    return;
  }

  if(user1TaskInfo.refPositionSet == 0U){
    user1TaskInfo.refLatitude = user1TaskInfo.gpsLatitude;
    user1TaskInfo.refLongitude = user1TaskInfo.gpsLongitude;
    user1TaskInfo.refPositionSet = 1U;
    return;
  }

  distanceM = calcDistanceM(user1TaskInfo.refLatitude,
                            user1TaskInfo.refLongitude,
                            user1TaskInfo.gpsLatitude,
                            user1TaskInfo.gpsLongitude);
  if(distanceM > ANTITHEFT_DISPLACE_THRESHOLD_M){
    user1TaskSetAlarm(USER1_ALARM_SOURCE_DISPLACE);
  }
}

/* 500ms刷新OLED显示 */
static void user1TaskOledRefresh(void){
  uint32_t sleepSec = (user1TaskInfo.sleepCountdownMs + 999U) / 1000U;

  if(user1TaskInfo.isSleeping != 0U){
    if(oledInfo.isOn != 0U){
      DRIVER_OLED_Clear();
      DRIVER_OLED_Refresh();
      DRIVER_OLED_DisplayOff();
    }
    return;
  }

  if(oledInfo.isOn == 0U){
    DRIVER_OLED_DisplayOn();
  }

  DRIVER_OLED_Clear();
  DRIVER_OLED_ShowString(0U, 0U, user1TaskGpsStatusText(user1TaskInfo.gpsStatus));
  DRIVER_OLED_ShowString(0U, 8U, "Lat:");
  DRIVER_OLED_ShowFloat12x16(0U, 16U, user1TaskInfo.gpsLatitude, 4U);
  DRIVER_OLED_ShowString(0U, 32U, "Lon:");
  DRIVER_OLED_ShowFloat12x16(0U, 40U, user1TaskInfo.gpsLongitude, 4U);
  DRIVER_OLED_ShowString(0U, 56U, "A:");
  DRIVER_OLED_ShowString(12U, 56U, user1TaskAlarmSourceText(user1TaskInfo.alarmSource));
  DRIVER_OLED_ShowString(78U, 56U, "S:");
  DRIVER_OLED_ShowNum(90U, 56U, sleepSec, 2U);
  DRIVER_OLED_ShowString(102U, 56U, "s");
  DRIVER_OLED_Refresh();
}

/*============================================================================
 * API接口
 *============================================================================*/

void user1TaskInit(void){
  DRIVER_BLE_Init();
  DRIVER_GPS_Init();

  /* 等待系统任务完成stdlib初始化，并等待OLED VCC上电稳定 */
  osDelay(100);
  DRIVER_BOARD_Init();
  DRIVER_OLED_Init();
  DRIVER_MPU6050_Init();

  user1TaskInfo.taskCnt = 0U;
  user1TaskInfo.gpsLatitude = 0.0f;
  user1TaskInfo.gpsLongitude = 0.0f;
  user1TaskInfo.gpsValid = 0U;
  user1TaskInfo.gpsStatus = (uint8_t)GPS_STATUS_DISCONNECTED;
  user1TaskInfo.antitheftEnabled = 0U;
  user1TaskInfo.alarmFlag = 0U;
  user1TaskInfo.alarmSource = USER1_ALARM_SOURCE_NONE;
  user1TaskInfo.refLatitude = 0.0f;
  user1TaskInfo.refLongitude = 0.0f;
  user1TaskInfo.refPositionSet = 0U;
  user1TaskInfo.sleepCountdownMs = USER1_SLEEP_TIMEOUT_MS;
  user1TaskInfo.isSleeping = 0U;
  user1TaskInfo.appAntitheftCmd = USER1_APP_CMD_NONE;
  user1TaskInfo.appAlarmClearCmd = 0U;

  user1GpsFrameSeq = 0U;
  user1VibrationCnt = 0U;
  user1AlarmOutputOn = 0U;
  user1Key1LastPressed = 0U;
}

void user1TaskUpdata(void *argument){
  (void)argument;
  user1TaskInit();
  for(;;){
    user1TaskInfo.taskCnt++;

    user1TaskSleepWakeProcess();

    if(user1TaskInfo.taskCnt % 5U == 0U){
      /* 10ms：需要周期读取的driver */
      DRIVER_MPU6050_Update();
    }

    if(user1TaskInfo.taskCnt % 50U == 0U){
      /* 100ms：APP指令、防盗检测、声光报警、状态上报 */
      user1TaskAppRxProcess();
      user1TaskVibrationDetect();
      user1TaskAlarmOutputProcess();
      user1TaskAppTxStatusWrite();
    }

    if(user1TaskInfo.taskCnt % 250U == 0U){
      /* 500ms：OLED刷新 */
      user1TaskOledRefresh();
    }

    if(user1TaskInfo.taskCnt % 500U == 0U){
      /* 1000ms：GPS读取、位移检测、GPS上报 */
      user1TaskGpsUpdateAndTxWrite();
      user1TaskDisplaceDetect();
    }

    osDelay(2);
  }
}
