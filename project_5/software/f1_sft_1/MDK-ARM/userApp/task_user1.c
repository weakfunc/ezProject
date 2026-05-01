#include "task_user1.h"
#include "driver_senser.h"
#include "driver_oled.h"
#include "driver_mpu6050.h"
#include "driver_gps.h"
#include "driver_a7670c.h"
#include "driver_XRVoice.h"
#include "driver_ds3231rtc.h"
#include "driver_board.h"
#include <math.h>
#include <stdio.h>

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* SMS 内部状态 */
#define SMS_STATE_INITING   0U
#define SMS_STATE_READY     1U
#define SMS_STATE_ERROR     2U
#define SMS_STATE_SENDING   3U
#define SMS_STATE_SENT_OK   4U
#define SMS_STATE_SENT_FAIL 5U

/* 跌倒检测内部状态 */
#define FALL_STATE_NORMAL   0U  /* 正常 */
#define FALL_STATE_IMPACT   1U  /* 检测到冲击，等待静止确认 */
#define FALL_STATE_FALLEN   2U  /* 已确认摔倒 */

/* 行走引导阶段 */
#define GUIDE_PHASE_CLEAR             0U  /* 无障碍，正常行走 */
#define GUIDE_PHASE_OBSTACLE_NOTIFIED 1U  /* 已播障碍提示，待播转向提示 */
#define GUIDE_PHASE_TURN_PROMPTED     2U  /* 已播转向提示，待记录 yaw */
#define GUIDE_PHASE_WAIT_TURN         3U  /* 等待陀螺仪检测到转向 */
#define GUIDE_PHASE_STRAIGHT          4U  /* 已播直行，持续监测障碍 */

/* XRVoice 语义编号 */
#define VOICE_SEM_OBSTACLE    11U  /* 前方有障碍物 */
#define VOICE_SEM_TURN_LEFT   12U  /* 左转 */
#define VOICE_SEM_TURN_RIGHT  13U  /* 右转 */
#define VOICE_SEM_STRAIGHT    14U  /* 直行 */
#define VOICE_SEM_FALLEN      15U  /* 检测到摔倒 */
#define VOICE_SEM_HOUR_BASE   16U  /* 语义16~27：1点整~12点整 */

/* 语音冷却时长（单位：50ms 调度次数，40次 = 2s） */
#define VOICE_COOLDOWN_50MS   40U

/* OLED 行坐标（每行 8px） */
#define OLED_LINE_H           8U
#define OLED_LINE(n)          ((uint8_t)((n) * OLED_LINE_H))

/* 摔倒短信缓冲区长度 */
#define FALL_SMS_BUF_LEN      120U
/* 上电测试短信延时：2500 × 2ms = 5s */
#define TEST_SMS_DELAY_TICKS  2500U
/* 上电测试短信内容 */
#define TEST_SMS_CONTENT      "STM32 test SMS OK"

/*============================================================================
 * 私有变量
 *============================================================================*/

/* task 公有信息结构体实例 */
user1TaskInfo_t user1TaskInfo;

/* SMS 模块状态 */
static uint8_t smsState = SMS_STATE_INITING;

/* 跌倒检测状态机 */
static uint8_t  fallDetState   = FALL_STATE_NORMAL;
static uint32_t fallConfirmCnt = 0U;

/* 语音冷却计数（单位：50ms 调度次数） */
static uint8_t voiceCooldown = 0U;

/* 行走引导状态机 */
static uint8_t guidePhase   = GUIDE_PHASE_CLEAR;
static float   yawAtTurnCmd = 0.0f;  /* 发出转向提示时记录的 yaw 角（°） */
static uint8_t guideTurnDir = 0U;    /* 转向方向切换：0=下次左转，1=下次右转 */

/* 整点待播小时（12h制，0=无待播） */
static uint8_t chimeHour = 0U;

/* 上电测试短信已发送标志 */
static uint8_t testSmsSent = 0U;

/* KEY1 上次累计按下次数，用于检测单次按下事件 */
static uint32_t key1LastPressCount = 0U;

/* KEY2 上次累计按下次数，用于检测单次按下事件 */
static uint32_t key2LastPressCount = 0U;

/* 摔倒短信缓冲区（静态分配） */
static char smsBuf[FALL_SMS_BUF_LEN];

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 判断当前是否存在需要避让的障碍物。 */
static uint8_t user1HasObstacle(void){
  if((user1TaskInfo.distanceValid != 0U) &&
     (user1TaskInfo.distance_cm   <  GUIDE_OBSTACLE_CM)){
    return 1U;
  }
  return 0U;
}

/* 发送指定语义 ID 的语音命令并启动冷却。 */
static uint8_t user1VoicePlay(uint8_t semId){
  uint8_t frame[XRVOICE_PROTO_FRAME_LEN];

  if(DRIVER_XRVOICE_FindBySemanticId(semId, frame) == 0U){
    return 0U;
  }

  DRIVER_XRVOICE_SendCmd(frame);
  voiceCooldown = VOICE_COOLDOWN_50MS;
  user1TaskInfo.voiceBusy = 1U;
  return 1U;
}

/* 更新语音冷却状态，返回 1 表示本周期可发起新播报。 */
static uint8_t user1VoiceIsReady(void){
  if(voiceCooldown > 0U){
    voiceCooldown--;
    user1TaskInfo.voiceBusy = 1U;
    return 0U;
  }

  user1TaskInfo.voiceBusy = 0U;
  return 1U;
}

/* 跌倒检测逻辑（10ms 周期调用）。 */
static void user1FallUpdate(void){
  mpu6050AccelData_t accel;
  float gTotal;

  if(DRIVER_MPU6050_GetAccel(&accel) == 0U){
    return;
  }

  gTotal = sqrtf(accel.xG * accel.xG + accel.yG * accel.yG + accel.zG * accel.zG);

  switch(fallDetState){
    case FALL_STATE_NORMAL:
      if(gTotal > FALL_THRESHOLD_HIGH){
        fallDetState   = FALL_STATE_IMPACT;
        fallConfirmCnt = 0U;
      }
      break;

    case FALL_STATE_IMPACT:
      if(gTotal < FALL_THRESHOLD_STATIC){
        fallConfirmCnt++;
        if(fallConfirmCnt >= FALL_CONFIRM_COUNT){
          fallDetState           = FALL_STATE_FALLEN;
          user1TaskInfo.isFallen = 1U;
          user1TaskInfo.oledNeedUpdate = 1U;
        }
      } else if(gTotal > FALL_THRESHOLD_HIGH){
        /* 持续震动时保持冲击状态，等震动短暂回落后确认摔倒 */
        fallConfirmCnt = 0U;
      } else {
        /* 动态恢复，视为误判 */
        fallDetState   = FALL_STATE_NORMAL;
        fallConfirmCnt = 0U;
      }
      break;

    case FALL_STATE_FALLEN:
      /* 检测到重新大幅运动时自动复位 */
      if(gTotal > FALL_RESET_THRESHOLD){
        fallDetState                = FALL_STATE_NORMAL;
        fallConfirmCnt              = 0U;
        user1TaskInfo.isFallen      = 0U;
        user1TaskInfo.fallAlertSent = 0U;
        user1TaskInfo.oledNeedUpdate = 1U;
      }
      break;

    default:
      fallDetState   = FALL_STATE_NORMAL;
      fallConfirmCnt = 0U;
      break;
  }
}

/* 清除摔倒检测状态，KEY1 手动复位时调用。 */
static void user1FallClear(void){
  fallDetState                = FALL_STATE_NORMAL;
  fallConfirmCnt              = 0U;
  user1TaskInfo.isFallen      = 0U;
  user1TaskInfo.fallAlertSent = 0U;
  user1TaskInfo.oledNeedUpdate = 1U;
}

/* 判断公历闰年。 */
static uint8_t user1IsLeapYear(uint16_t year){
  if((year % 400U) == 0U){
    return 1U;
  }
  if((year % 100U) == 0U){
    return 0U;
  }
  return ((year % 4U) == 0U) ? 1U : 0U;
}

/* 获取指定年月的天数。 */
static uint8_t user1DaysInMonth(uint16_t year, uint8_t month){
  static const uint8_t daysInMonth[12] = {
    31U, 28U, 31U, 30U, 31U, 30U,
    31U, 31U, 30U, 31U, 30U, 31U
  };

  if((month == 0U) || (month > 12U)){
    return 31U;
  }

  if((month == 2U) && (user1IsLeapYear(year) != 0U)){
    return 29U;
  }

  return daysInMonth[month - 1U];
}

/* KEY2：将 RTC 时间四舍五入到最近整点，并触发对应整点语音。 */
static void user1RoundRtcToNearestHour(void){
  ds3231RTCTime_t roundedTime;
  uint8_t hour;
  uint8_t minute;
  uint8_t date;
  uint8_t month;
  uint8_t yearLow;
  uint16_t fullYear;
  uint8_t hour12;

  if(ds3231RTCInfo.isReady == 0U){
    DRIVER_DS3231RTC_Update();
  }

  if(ds3231RTCInfo.isReady == 0U){
    return;
  }

  roundedTime = ds3231RTCInfo.time;

  hour     = DS3231RTC_BCD_TO_DEC(roundedTime.hour);
  minute   = DS3231RTC_BCD_TO_DEC(roundedTime.minute);
  date     = DS3231RTC_BCD_TO_DEC(roundedTime.date);
  month    = DS3231RTC_BCD_TO_DEC(roundedTime.month);
  yearLow  = DS3231RTC_BCD_TO_DEC(roundedTime.year);
  fullYear = (uint16_t)(2000U + yearLow);

  if(minute >= 30U){
    hour++;
    if(hour >= 24U){
      hour = 0U;

      date++;
      if(date > user1DaysInMonth(fullYear, month)){
        date = 1U;
        month++;
        if(month > 12U){
          month = 1U;
          yearLow = (uint8_t)((yearLow + 1U) % 100U);
          fullYear = (uint16_t)(2000U + yearLow);
        }
      }

      roundedTime.day = (uint8_t)(DS3231RTC_DEC_TO_BCD((DS3231RTC_BCD_TO_DEC(roundedTime.day) % 7U) + 1U));
    }
  }

  roundedTime.second = DS3231RTC_DEC_TO_BCD(0U);
  roundedTime.minute = DS3231RTC_DEC_TO_BCD(0U);
  roundedTime.hour   = DS3231RTC_DEC_TO_BCD(hour);
  roundedTime.date   = DS3231RTC_DEC_TO_BCD(date);
  roundedTime.month  = DS3231RTC_DEC_TO_BCD(month);
  roundedTime.year   = DS3231RTC_DEC_TO_BCD(yearLow);

  DRIVER_DS3231RTC_SetTime(&roundedTime);

  user1TaskInfo.lastHour = hour;
  hour12 = (uint8_t)(hour % 12U);
  if(hour12 == 0U){ hour12 = 12U; }
  voiceCooldown = 0U;
  user1TaskInfo.voiceBusy = 0U;
  if(user1VoicePlay((uint8_t)(VOICE_SEM_HOUR_BASE + hour12 - 1U)) == 0U){
    chimeHour = hour12;
  } else {
    chimeHour = 0U;
  }
  user1TaskInfo.oledNeedUpdate = 1U;
}

/* 按键更新：KEY1 清除摔倒，KEY2 校时到最近整点。 */
static void user1KeyUpdate(void){
  boardKeyInfo_t keyInfo;

  DRIVER_BOARD_KeyInfoUpdate();

  if(DRIVER_BOARD_KeyInfoGet(BOARD_KEY1, &keyInfo) == 0U){
    keyInfo.pressCount = key1LastPressCount;
  }

  if(keyInfo.pressCount != key1LastPressCount){
    key1LastPressCount = keyInfo.pressCount;
    user1FallClear();
  }

  if(DRIVER_BOARD_KeyInfoGet(BOARD_KEY2, &keyInfo) == 0U){
    return;
  }

  if(keyInfo.pressCount != key2LastPressCount){
    key2LastPressCount = keyInfo.pressCount;
    user1RoundRtcToNearestHour();
  }
}

/* 语音播报调度（50ms 周期调用）。
 * 优先级：摔倒报警 > 障碍行走引导 > 整点报时。
 */
static void user1VoiceDispatch(void){
  float yawDiff;
  uint8_t semId;

  if(user1VoiceIsReady() == 0U){
    return;
  }

  /* 优先级1：摔倒报警 */
  if(user1TaskInfo.isFallen != 0U){
    chimeHour = 0U;
    (void)user1VoicePlay(VOICE_SEM_FALLEN);
    return;
  }

  /* 优先级2：行走引导状态机 */
  if(user1HasObstacle() != 0U){
    chimeHour = 0U;

    switch(guidePhase){
      case GUIDE_PHASE_CLEAR:
        if(user1VoicePlay(VOICE_SEM_OBSTACLE) != 0U){
          guidePhase = GUIDE_PHASE_OBSTACLE_NOTIFIED;
        }
        break;

      case GUIDE_PHASE_OBSTACLE_NOTIFIED:
        guideTurnDir ^= 1U;
        semId = (guideTurnDir != 0U) ? VOICE_SEM_TURN_RIGHT : VOICE_SEM_TURN_LEFT;
        if(user1VoicePlay(semId) != 0U){
          guidePhase = GUIDE_PHASE_TURN_PROMPTED;
        }
        break;

      case GUIDE_PHASE_TURN_PROMPTED:
        yawAtTurnCmd = mpu6050Info.angle.yawDeg;
        guidePhase   = GUIDE_PHASE_WAIT_TURN;
        break;

      case GUIDE_PHASE_WAIT_TURN:
        yawDiff = mpu6050Info.angle.yawDeg - yawAtTurnCmd;
        if(yawDiff < 0.0f){ yawDiff = -yawDiff; }
        if(yawDiff > GUIDE_TURN_THRESHOLD_DEG){
          if(user1VoicePlay(VOICE_SEM_STRAIGHT) != 0U){
            guidePhase = GUIDE_PHASE_STRAIGHT;
          }
        }
        break;

      case GUIDE_PHASE_STRAIGHT:
        if(user1VoicePlay(VOICE_SEM_OBSTACLE) != 0U){
          guidePhase = GUIDE_PHASE_OBSTACLE_NOTIFIED;
        }
        break;

      default:
        guidePhase = GUIDE_PHASE_CLEAR;
        break;
    }
    return;
  }

  guidePhase = GUIDE_PHASE_CLEAR;

  /* 优先级3：整点报时；忙碌时不补报，chimeHour 只保存当前整点瞬间。 */
  if(chimeHour != 0U){
    semId = (uint8_t)(VOICE_SEM_HOUR_BASE + chimeHour - 1U);
    (void)user1VoicePlay(semId);
    chimeHour = 0U;
  }
}

/* 整点报时检查（500ms 周期调用）。 */
static void user1ChimeCheck(void){
  uint8_t hour;
  uint8_t minute;
  uint8_t hour12;

  if(ds3231RTCInfo.isReady == 0U){
    return;
  }

  hour   = DS3231RTC_BCD_TO_DEC(ds3231RTCInfo.time.hour);
  minute = DS3231RTC_BCD_TO_DEC(ds3231RTCInfo.time.minute);

  if((minute == 0U) && (hour != user1TaskInfo.lastHour)){
    user1TaskInfo.lastHour = hour;
    hour12 = (uint8_t)(hour % 12U);
    if(hour12 == 0U){ hour12 = 12U; }

    if((user1TaskInfo.voiceBusy == 0U) &&
       (user1TaskInfo.isFallen  == 0U) &&
       (user1HasObstacle()      == 0U)){
      chimeHour = hour12;
    } else {
      chimeHour = 0U;
    }
  }
}

/* 刷新 OLED 全屏（50ms 周期调用）。 */
static void user1OledRefresh(void){
  char    buf[22];
  uint8_t hour;
  uint8_t minute;
  uint8_t date;
  uint8_t month;

  DRIVER_OLED_Clear();

  if(user1TaskInfo.distanceValid != 0U){
    snprintf(buf, sizeof(buf), "D:%5.1fcm", (double)user1TaskInfo.distance_cm);
  } else {
    snprintf(buf, sizeof(buf), "D:---  cm");
  }
  DRIVER_OLED_ShowString(0U, OLED_LINE(0U), buf);

  DRIVER_OLED_ShowString(0U, OLED_LINE(1U),
    (user1TaskInfo.gpsFixed != 0U) ? "GPS:OK  " : "GPS:Srch");

  snprintf(buf, sizeof(buf), "Lat:%8.4f", (double)user1TaskInfo.latitude);
  DRIVER_OLED_ShowString(0U, OLED_LINE(2U), buf);

  snprintf(buf, sizeof(buf), "Lon:%8.4f", (double)user1TaskInfo.longitude);
  DRIVER_OLED_ShowString(0U, OLED_LINE(3U), buf);

  DRIVER_OLED_ShowString(0U, OLED_LINE(4U),
    (user1TaskInfo.isFallen != 0U) ? "Fall:!FALL!" : "Fall:OK");

  switch(smsState){
    case SMS_STATE_INITING:  DRIVER_OLED_ShowString(0U, OLED_LINE(5U), "SMS:INIT"); break;
    case SMS_STATE_READY:    DRIVER_OLED_ShowString(0U, OLED_LINE(5U), "SMS:RDY");  break;
    case SMS_STATE_ERROR:    DRIVER_OLED_ShowString(0U, OLED_LINE(5U), "SMS:ERR");  break;
    case SMS_STATE_SENDING:  DRIVER_OLED_ShowString(0U, OLED_LINE(5U), "SMS:SEND"); break;
    case SMS_STATE_SENT_OK:  DRIVER_OLED_ShowString(0U, OLED_LINE(5U), "SMS:OK");   break;
    default:                 DRIVER_OLED_ShowString(0U, OLED_LINE(5U), "SMS:FAIL"); break;
  }

  /* 行6：日期和时间放在同一行，避免占用两行显示空间。 */
  if(ds3231RTCInfo.isReady != 0U){
    date   = DS3231RTC_BCD_TO_DEC(ds3231RTCInfo.time.date);
    month  = DS3231RTC_BCD_TO_DEC(ds3231RTCInfo.time.month);
    hour   = DS3231RTC_BCD_TO_DEC(ds3231RTCInfo.time.hour);
    minute = DS3231RTC_BCD_TO_DEC(ds3231RTCInfo.time.minute);
    snprintf(buf, sizeof(buf), "20%02u-%02u-%02u %02u:%02u",
             (unsigned)DS3231RTC_BCD_TO_DEC(ds3231RTCInfo.time.year),
             (unsigned)month, (unsigned)date,
             (unsigned)hour, (unsigned)minute);
  } else {
    snprintf(buf, sizeof(buf), "20??-??-?? --:--");
  }
  DRIVER_OLED_ShowString(0U, OLED_LINE(6U), buf);

  /* 行7：显示陀螺仪融合角度，便于观察转向和姿态变化。 */
  if(mpu6050Info.isReady != 0U){
    snprintf(buf, sizeof(buf), "Y:%5.1f P:%5.1f",
             (double)mpu6050Info.angle.yawDeg,
             (double)mpu6050Info.angle.pitchDeg);
  } else {
    snprintf(buf, sizeof(buf), "Y: ---- P: ----");
  }
  DRIVER_OLED_ShowString(0U, OLED_LINE(7U), buf);

  DRIVER_OLED_Refresh();
  user1TaskInfo.oledNeedUpdate = 0U;
}

/* OLED 单行提示（Init 阶段过渡画面）。 */
static void user1OledShowInitMsg(const char *msg){
  DRIVER_OLED_Clear();
  DRIVER_OLED_ShowString(0U, OLED_LINE(0U), msg);
  DRIVER_OLED_Refresh();
}

/* 500ms 周期读取 GPS 快照。 */
static void user1GpsUpdate(void){
  gpsInfo_t gpsSnapshot;

  if(DRIVER_GPS_GetInfo(&gpsSnapshot) != 0U){
    user1TaskInfo.gpsFixed  = gpsSnapshot.hasLocation;
    user1TaskInfo.latitude  = gpsSnapshot.latitudeDeg;
    user1TaskInfo.longitude = gpsSnapshot.longitudeDeg;
  } else {
    user1TaskInfo.gpsFixed = 0U;
  }

  user1TaskInfo.oledNeedUpdate = 1U;
}

/* 发送上电测试短信，仅发送一次。 */
static void user1TestSmsCheck(void){
  uint8_t smsResult;

  if(testSmsSent != 0U){
    return;
  }

  if(user1TaskInfo.taskCnt < TEST_SMS_DELAY_TICKS){
    return;
  }

  if((smsState != SMS_STATE_READY)    &&
     (smsState != SMS_STATE_SENT_OK)  &&
     (smsState != SMS_STATE_SENT_FAIL)){
    return;
  }

  smsState = SMS_STATE_SENDING;
  user1TaskInfo.oledNeedUpdate = 1U;

  smsResult = DRIVER_A7670C_SendSms(ALERT_PHONE_NUMBER, TEST_SMS_CONTENT);
  smsState = (smsResult == A7670C_SMS_OK) ? SMS_STATE_SENT_OK : SMS_STATE_SENT_FAIL;
  testSmsSent = 1U;
  user1TaskInfo.oledNeedUpdate = 1U;
}

/* 1s 周期检查并发送摔倒短信。 */
static void user1SmsCheck(void){
  uint8_t smsResult;

  if((user1TaskInfo.isFallen      == 0U) ||
     (user1TaskInfo.fallAlertSent != 0U)){
    return;
  }

  if((smsState != SMS_STATE_READY)    &&
     (smsState != SMS_STATE_SENT_OK)  &&
     (smsState != SMS_STATE_SENT_FAIL)){
    return;
  }

  smsState = SMS_STATE_SENDING;
  user1TaskInfo.oledNeedUpdate = 1U;

  if(user1TaskInfo.gpsFixed != 0U){
    snprintf(smsBuf, FALL_SMS_BUF_LEN,
             "紧急警报!用户摔倒!位置:纬度%.4f,经度%.4f",
             (double)user1TaskInfo.latitude,
             (double)user1TaskInfo.longitude);
  } else {
    snprintf(smsBuf, FALL_SMS_BUF_LEN,
             "紧急警报!用户摔倒!GPS未定位,无法获取坐标");
  }

  smsResult = DRIVER_A7670C_SendSms(ALERT_PHONE_NUMBER, smsBuf);
  if(smsResult == A7670C_SMS_OK){
    smsState = SMS_STATE_SENT_OK;
    user1TaskInfo.fallAlertSent = 1U;
  } else {
    smsState = SMS_STATE_SENT_FAIL;
  }

  user1TaskInfo.oledNeedUpdate = 1U;
}

/*============================================================================
 * 公有函数
 *============================================================================*/

void user1TaskInit(void){
  DRIVER_SENSER_Init();
  DRIVER_OLED_Init();
  DRIVER_MPU6050_Init();
  DRIVER_GPS_Init();
  DRIVER_XRVOICE_Init();
  DRIVER_DS3231RTC_Init();
  DRIVER_BOARD_Init();

  user1TaskInfo.taskCnt         = 0U;
  user1TaskInfo.distance_cm     = 0.0f;
  user1TaskInfo.distanceValid   = 0U;
  user1TaskInfo.isFallen        = 0U;
  user1TaskInfo.fallAlertSent   = 0U;
  user1TaskInfo.gpsFixed        = 0U;
  user1TaskInfo.latitude        = 0.0f;
  user1TaskInfo.longitude       = 0.0f;
  user1TaskInfo.lastHour        = 0xFFU;
  user1TaskInfo.voiceBusy       = 0U;
  user1TaskInfo.oledNeedUpdate  = 1U;

  fallDetState   = FALL_STATE_NORMAL;
  fallConfirmCnt = 0U;
  voiceCooldown  = 0U;
  guidePhase     = GUIDE_PHASE_CLEAR;
  chimeHour      = 0U;
  testSmsSent    = 0U;
  key1LastPressCount = 0U;
  key2LastPressCount = 0U;

  smsState = SMS_STATE_INITING;
  user1OledShowInitMsg("SMS:INIT...");
  DRIVER_A7670C_Init();
  smsState = (a7670cInfo.diag.allPass != 0U) ? SMS_STATE_READY : SMS_STATE_ERROR;
  user1OledShowInitMsg((smsState == SMS_STATE_READY) ? "SMS:READY" : "SMS:ERROR");
}

void user1TaskUpdata(void *argument){
  (void)argument;

  user1TaskInit();

  for(;;){
    user1TaskInfo.taskCnt++;

    /* ---- 10ms 周期：超声波测距 + 加速度更新 + 跌倒检测 ---- */
    if((user1TaskInfo.taskCnt % 5U) == 0U){
      if(DRIVER_SENSER_GetHCSR04Distance(NULL) != 0U){
        user1TaskInfo.distance_cm   = (float)senserInfo.hcsrDistMm / 10.0f;
        user1TaskInfo.distanceValid = 1U;
      } else {
        user1TaskInfo.distanceValid = 0U;
      }

      DRIVER_MPU6050_Update();
      user1FallUpdate();
      user1KeyUpdate();
      user1TaskInfo.oledNeedUpdate = 1U;
    }

    /* ---- 50ms 周期：语音轮询 + 播报调度 + OLED 刷新 ---- */
    if((user1TaskInfo.taskCnt % 25U) == 0U){
      if(DRIVER_XRVOICE_Poll() != 0U){
        xrVoiceInfo.cmdReady = 0U;
      }

      user1VoiceDispatch();

      if(user1TaskInfo.oledNeedUpdate != 0U){
        user1OledRefresh();
      }
    }

    /* ---- 500ms 周期：GPS 快照 + DS3231 更新时间 + 整点检查 ---- */
    if((user1TaskInfo.taskCnt % 250U) == 0U){
      user1GpsUpdate();
      DRIVER_DS3231RTC_Update();
      user1ChimeCheck();
      user1TaskInfo.oledNeedUpdate = 1U;
    }

    /* ---- 1s 周期：摔倒短信发送 ---- */
    if((user1TaskInfo.taskCnt % 500U) == 0U){
      user1TestSmsCheck();
      if(testSmsSent != 0U){
        user1SmsCheck();
      }
    }

    osDelay(2U);
  }
}
