#include "task_user1.h"
#include "driver_ble.h"
#include "driver_mp3.h"
#include "driver_tjcLCD.h"
#include "driver_ws2812.h"
#include "func_appcom.h"
#include "stdlib_tim.h"

#include <stdio.h>
#include <string.h>

/* 夜灯 PWM 通道 */
#define LIGHT_PWM_CH                PWM_TIM4_CH1

/* WS2812 夜灯配置 */
#define LIGHT_WS2812_CH0            WS2812_CH_0
#define LIGHT_WS2812_CH1            WS2812_CH_1
#define LIGHT_WS2812_GPIO0          GPIO_ID_USER_IO_1
#define LIGHT_WS2812_GPIO1          GPIO_ID_USER_IO_2
#define LIGHT_WS2812_LED_COUNT      16U

/* WS2812 时序测试开关。
 * 置 1 后，任务会每 50ms 强制发送一帧固定测试图案，便于示波器抓时序。 */
#define LIGHT_EFFECT_BREATH_TICK    5U

/* APP fixed-slot protocol */
#define APP_REMOTE_INVALID_U32      (0xFFFFFFFFUL)

/* MP3 硬件音量范围 */
#define MP3_VOLUME_MAX              30U

/* 定时/灯光档位表长度 */
#define TIMER_CYCLE_COUNT           7U
#define LIGHT_CYCLE_COUNT           5U

/* 等待 systemTask 完成底层初始化，避免 DWT/PWM/UART 未就绪 */
#define SYSTEM_READY_DELAY_MS       20U

typedef struct {
  const uint8_t *data;
  uint8_t        len;
} screenText_t;

typedef struct {
  uint8_t track;
  uint8_t light;
  uint8_t lightMode;
} scenePreset_t;

typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} lightColor_t;

typedef enum {
  LIGHT_EFFECT_STATIC = 0,
  LIGHT_EFFECT_BREATH
} lightEffect_e;

typedef enum {
  LIGHT_MODE_BLUE = 0,
  LIGHT_MODE_JADE,
  LIGHT_MODE_VIOLET_BREATH,
  LIGHT_MODE_PINK_BREATH,
  LIGHT_MODE_COUNT
} lightMode_e;

typedef struct {
  const char   *label;
  lightColor_t  color;
  uint8_t       effect;
  uint8_t       speed;
} lightModePreset_t;

/* 曲目名，GB2312 编码 */
static const uint8_t trackName1[]  = {0xD3, 0xEA, 0xC9, 0xF9};
static const uint8_t trackName2[]  = {0xC9, 0xAD, 0xC1, 0xD6};
static const uint8_t trackName3[]  = {0xBA, 0xA3, 0xC0, 0xCB};
static const uint8_t trackName4[]  = {0xCF, 0xAA, 0xC1, 0xF7};
static const uint8_t trackName5[]  = {0xC4, 0xF1, 0xC3, 0xF9};
static const uint8_t trackName6[]  = {0xB0, 0xD7, 0xD4, 0xEB};
static const uint8_t trackName7[]  = {0xBF, 0xA7, 0xB7, 0xC8, 0xB9, 0xDD};
static const uint8_t trackName8[]  = {0xC0, 0xD7, 0xD3, 0xEA};
static const uint8_t trackName9[]  = {0xB7, 0xE7, 0xC9, 0xF9};
static const uint8_t trackName10[] = {0xB3, 0xE6, 0xC3, 0xF9};
static const uint8_t trackName11[] = {0xD3, 0xEA, 0xC9, 0xF9, 0x2B, 0xC4, 0xF1, 0xC3, 0xF9};
static const uint8_t trackName12[] = {0xC9, 0xAD, 0xC1, 0xD6, 0x2B, 0xCF, 0xAA, 0xC1, 0xF7};
static const uint8_t trackName13[] = {0xBA, 0xA3, 0xC0, 0xCB, 0x2B, 0xB7, 0xE7, 0xC9, 0xF9};
static const uint8_t trackName14[] = {0xF3, 0xF4, 0xBB, 0xF0};
static const uint8_t trackName15[] = {0xB3, 0xC7, 0xCA, 0xD0, 0xD2, 0xB9, 0xBE, 0xB0};

/* 模式名与通用显示文本，GB2312 编码 */
static const uint8_t modeNameSleep[]    = {0xD6, 0xFA, 0xC3, 0xDF};
static const uint8_t modeNameFocus[]    = {0xD7, 0xA8, 0xD7, 0xA2};
static const uint8_t modeNameMeditate[] = {0xDA, 0xA4, 0xCF, 0xEB};
static const uint8_t modeNameBaby[]     = {0xBA, 0xE5, 0xCB, 0xAF};
static const uint8_t textOff[]          = {0xB9, 0xD8};

static const screenText_t trackNameTable[TRACK_COUNT] = {
  {trackName1,  sizeof(trackName1)},
  {trackName2,  sizeof(trackName2)},
  {trackName3,  sizeof(trackName3)},
  {trackName4,  sizeof(trackName4)},
  {trackName5,  sizeof(trackName5)},
  {trackName6,  sizeof(trackName6)},
  {trackName7,  sizeof(trackName7)},
  {trackName8,  sizeof(trackName8)},
  {trackName9,  sizeof(trackName9)},
  {trackName10, sizeof(trackName10)},
  {trackName11, sizeof(trackName11)},
  {trackName12, sizeof(trackName12)},
  {trackName13, sizeof(trackName13)},
  {trackName14, sizeof(trackName14)},
  {trackName15, sizeof(trackName15)}
};

static const screenText_t modeNameTable[MODE_COUNT] = {
  {modeNameSleep,    sizeof(modeNameSleep)},
  {modeNameFocus,    sizeof(modeNameFocus)},
  {modeNameMeditate, sizeof(modeNameMeditate)},
  {modeNameBaby,     sizeof(modeNameBaby)}
};

static const uint8_t timerCycleTable[TIMER_CYCLE_COUNT] = {
  TIMER_OFF, TIMER_1MIN, TIMER_5MIN, TIMER_10MIN,
  TIMER_15MIN, TIMER_30MIN, TIMER_60MIN
};

static const uint8_t lightCycleTable[LIGHT_CYCLE_COUNT] = {
  0U, 25U, 50U, 75U, 100U
};

static const scenePreset_t scenePresetTable[MODE_COUNT] = {
  {1U, 25U, LIGHT_MODE_BLUE},
  {6U, 35U, LIGHT_MODE_JADE},
  {4U, 45U, LIGHT_MODE_VIOLET_BREATH},
  {5U, 30U, LIGHT_MODE_PINK_BREATH}
};

static const lightModePreset_t lightModeTable[LIGHT_MODE_COUNT] = {
  {"BLUE",  {0U,   64U, 255U}, LIGHT_EFFECT_STATIC,  0U},
  {"JADE",  {0U,  220U, 80U},  LIGHT_EFFECT_STATIC,  0U},
  {"VIO-B", {170U, 0U,  220U}, LIGHT_EFFECT_BREATH, 10U},
  {"PNK-B", {255U, 10U, 120U}, LIGHT_EFFECT_BREATH,  8U},
};

static uint8_t currentLightMode = (uint8_t)LIGHT_MODE_BLUE;

user1TaskInfo_t user1TaskInfo;

static uint8_t getMp3Volume(uint8_t volumePercent);
static uint8_t isValidTimerSetting(uint8_t minutes);
static uint8_t isValidAppTimerSetting(uint32_t minutes);
static uint8_t getNextLightBrightness(uint8_t currentBrightness);
static uint8_t getScaledLightColor(uint8_t colorValue, uint8_t brightnessPercent);
static uint8_t getCurrentLightEffect(void);
static void clearWs2812Light(void);
static void refreshWs2812Light(void);
static void updateWs2812BreathEffect(void);
static void setAllDirty(void);
static void doPlayPause(void);
static void doPrev(void);
static void doNext(void);
static void doPlayTrack(uint8_t track);
static void doSetVolume(uint8_t volume);
static void doSetTimer(uint8_t minutes);
static void doSetLight(uint8_t brightness);
static void doSetMode(uint8_t mode);
static void cycleTimer(void);
static void cycleLight(void);
static void cycleMode(void);
static void handleScreenInput(void);
static void handleAppInput(void);
static void updateScreenDisplay(void);
static void updateAppTxVars(void);
static void handleTimerCountdown(void);

static uint8_t getMp3Volume(uint8_t volumePercent){
  return (uint8_t)(((uint32_t)volumePercent * MP3_VOLUME_MAX) / VOLUME_MAX);
}

static uint8_t isValidTimerSetting(uint8_t minutes){
  if((minutes == TIMER_OFF)  || (minutes == TIMER_1MIN) ||
     (minutes == TIMER_5MIN)  || (minutes == TIMER_10MIN) ||
     (minutes == TIMER_15MIN) || (minutes == TIMER_30MIN) ||
     (minutes == TIMER_60MIN)){
    return 1U;
  }
  return 0U;
}

static uint8_t isValidAppTimerSetting(uint32_t minutes){
  if((minutes == TIMER_OFF)  || (minutes == TIMER_5MIN)  ||
     (minutes == TIMER_10MIN) || (minutes == TIMER_15MIN) ||
     (minutes == TIMER_30MIN) || (minutes == TIMER_60MIN)){
    return 1U;
  }
  return 0U;
}

static uint8_t getNextLightBrightness(uint8_t currentBrightness){
  uint8_t i;

  for(i = 0U; i < LIGHT_CYCLE_COUNT; i++){
    if(currentBrightness < lightCycleTable[i]){
      return lightCycleTable[i];
    }
    if(currentBrightness == lightCycleTable[i]){
      if(i >= (LIGHT_CYCLE_COUNT - 1U)){
        return lightCycleTable[0];
      }
      return lightCycleTable[i + 1U];
    }
  }

  return lightCycleTable[0];
}

static uint8_t getScaledLightColor(uint8_t colorValue, uint8_t brightnessPercent){
  return (uint8_t)(((uint32_t)colorValue * brightnessPercent) / LIGHT_MAX);
}

static uint8_t getCurrentLightEffect(void){
  return lightModeTable[currentLightMode].effect;
}

static void clearWs2812Light(void){
  DRIVER_WS2812_Clear(LIGHT_WS2812_CH0);
  DRIVER_WS2812_Refresh(LIGHT_WS2812_CH0, LIGHT_WS2812_LED_COUNT);
  DRIVER_WS2812_Clear(LIGHT_WS2812_CH1);
  DRIVER_WS2812_Refresh(LIGHT_WS2812_CH1, LIGHT_WS2812_LED_COUNT);
}

static void refreshWs2812Light(void){
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  const lightModePreset_t *lightPreset;

  lightPreset = &lightModeTable[currentLightMode];
  if(user1TaskInfo.lightBrightness == LIGHT_MIN){
    clearWs2812Light();
    return;
  }

  red   = getScaledLightColor(lightPreset->color.red, user1TaskInfo.lightBrightness);
  green = getScaledLightColor(lightPreset->color.green, user1TaskInfo.lightBrightness);
  blue  = getScaledLightColor(lightPreset->color.blue, user1TaskInfo.lightBrightness);

  switch(lightPreset->effect){
    case LIGHT_EFFECT_BREATH:
      DRIVER_WS2812_BreathSetup(LIGHT_WS2812_CH0, red, green, blue,
                                LIGHT_WS2812_LED_COUNT, lightPreset->speed);
      DRIVER_WS2812_BreathSetup(LIGHT_WS2812_CH1, red, green, blue,
                                LIGHT_WS2812_LED_COUNT, lightPreset->speed);
      DRIVER_WS2812_BreathUpdate(LIGHT_WS2812_CH0);
      DRIVER_WS2812_BreathUpdate(LIGHT_WS2812_CH1);
      break;

    case LIGHT_EFFECT_STATIC:
    default:
      DRIVER_WS2812_SetAllColor(LIGHT_WS2812_CH0, red, green, blue);
      DRIVER_WS2812_Refresh(LIGHT_WS2812_CH0, LIGHT_WS2812_LED_COUNT);
      DRIVER_WS2812_SetAllColor(LIGHT_WS2812_CH1, red, green, blue);
      DRIVER_WS2812_Refresh(LIGHT_WS2812_CH1, LIGHT_WS2812_LED_COUNT);
      break;
  }
}

static void updateWs2812BreathEffect(void){
  if(user1TaskInfo.lightBrightness == LIGHT_MIN) return;
  if(getCurrentLightEffect() != LIGHT_EFFECT_BREATH) return;

  DRIVER_WS2812_BreathUpdate(LIGHT_WS2812_CH0);
  DRIVER_WS2812_BreathUpdate(LIGHT_WS2812_CH1);
}

static void setAllDirty(void){
  user1TaskInfo.dirtyTrack  = 1U;
  user1TaskInfo.dirtyVolume = 1U;
  user1TaskInfo.dirtyTimer  = 1U;
  user1TaskInfo.dirtyLight  = 1U;
  user1TaskInfo.dirtyMode   = 1U;
}

static void doPlayPause(void){
  if(user1TaskInfo.playState == PLAY_STATE_PLAYING){
    DRIVER_MP3_Pause();
    user1TaskInfo.playState = PLAY_STATE_PAUSED;
  }else{
    if(user1TaskInfo.playState == PLAY_STATE_STOPPED){
      DRIVER_MP3_PlayTrack(user1TaskInfo.currentTrack);
      user1TaskInfo.dirtyTrack = 1U;
    }else{
      DRIVER_MP3_Play();
    }
    user1TaskInfo.playState = PLAY_STATE_PLAYING;
  }
}

static void doPrev(void){
  if(user1TaskInfo.currentTrack <= 1U){
    user1TaskInfo.currentTrack = TRACK_COUNT;
  }else{
    user1TaskInfo.currentTrack--;
  }

  DRIVER_MP3_PlayTrack(user1TaskInfo.currentTrack);
  user1TaskInfo.playState = PLAY_STATE_PLAYING;
  user1TaskInfo.dirtyTrack = 1U;
}

static void doNext(void){
  if(user1TaskInfo.currentTrack >= TRACK_COUNT){
    user1TaskInfo.currentTrack = 1U;
  }else{
    user1TaskInfo.currentTrack++;
  }

  DRIVER_MP3_PlayTrack(user1TaskInfo.currentTrack);
  user1TaskInfo.playState = PLAY_STATE_PLAYING;
  user1TaskInfo.dirtyTrack = 1U;
}

static void doPlayTrack(uint8_t track){
  if((track < 1U) || (track > TRACK_COUNT)) return;

  user1TaskInfo.currentTrack = track;
  DRIVER_MP3_PlayTrack(track);
  user1TaskInfo.playState = PLAY_STATE_PLAYING;
  user1TaskInfo.dirtyTrack = 1U;
}

static void doSetVolume(uint8_t volume){
  if(volume > VOLUME_MAX){
    volume = VOLUME_MAX;
  }

  user1TaskInfo.volume = volume;
  DRIVER_MP3_SetVolume(getMp3Volume(volume));
  user1TaskInfo.dirtyVolume = 1U;
}

static void doSetTimer(uint8_t minutes){
  if(isValidTimerSetting(minutes) == 0U) return;

  user1TaskInfo.timerSetting = minutes;

  if(minutes == TIMER_OFF){
    user1TaskInfo.timerState = TIMER_STATE_IDLE;
    user1TaskInfo.timerRemainSec = 0U;
  }else{
    user1TaskInfo.timerState = TIMER_STATE_RUNNING;
    user1TaskInfo.timerRemainSec = (uint32_t)minutes * 60U;
  }

  user1TaskInfo.dirtyTimer = 1U;
}

static void doSetLight(uint8_t brightness){
  if(brightness > LIGHT_MAX){
    brightness = LIGHT_MAX;
  }

  user1TaskInfo.lightBrightness = brightness;
  refreshWs2812Light();
  STDLIB_TIM_PwmSetDuty(LIGHT_PWM_CH, (float)brightness);
  user1TaskInfo.dirtyLight = 1U;
}

static void doSetMode(uint8_t mode){
  if(mode >= MODE_COUNT) return;

  user1TaskInfo.currentMode = (sceneMode_e)mode;
  currentLightMode = scenePresetTable[mode].lightMode;
  doPlayTrack(scenePresetTable[mode].track);
  doSetLight(scenePresetTable[mode].light);
  user1TaskInfo.dirtyMode = 1U;
}

static void cycleTimer(void){
  uint8_t i;

  for(i = 0U; i < TIMER_CYCLE_COUNT; i++){
    if(user1TaskInfo.timerSetting == timerCycleTable[i]){
      if(i >= (TIMER_CYCLE_COUNT - 1U)){
        doSetTimer(timerCycleTable[0]);
      }else{
        doSetTimer(timerCycleTable[i + 1U]);
      }
      return;
    }
  }

  doSetTimer(timerCycleTable[0]);
}

static void cycleLight(void){
  doSetLight(getNextLightBrightness(user1TaskInfo.lightBrightness));
}

static void cycleMode(void){
  doSetMode(((uint8_t)user1TaskInfo.currentMode + 1U) % MODE_COUNT);
}

static void handleScreenInput(void){
  tjcLcdFrame_t frame;
  uint8_t nextVolume;

  if(DRIVER_TJCLCD_GetRxFrame(&frame) == 0U) return;

  if(frame.ctrl == 0x05U){
    if(memcmp(frame.data, "PREV", 4U) == 0){
      doPrev();
    }else if(memcmp(frame.data, "PLAY", 4U) == 0){
      doPlayPause();
    }else if(memcmp(frame.data, "NEXT", 4U) == 0){
      doNext();
    }else if(memcmp(frame.data, "VOL+", 4U) == 0){
      nextVolume = user1TaskInfo.volume + VOLUME_STEP;
      if(nextVolume > VOLUME_MAX){
        nextVolume = VOLUME_MAX;
      }
      doSetVolume(nextVolume);
    }else if(memcmp(frame.data, "VOL-", 4U) == 0){
      if(user1TaskInfo.volume >= VOLUME_STEP){
        nextVolume = user1TaskInfo.volume - VOLUME_STEP;
      }else{
        nextVolume = VOLUME_MIN;
      }
      doSetVolume(nextVolume);
    }
  }else if(frame.ctrl == 0x06U){
    if(memcmp(frame.data, "TIME", 4U) == 0){
      cycleTimer();
    }
  }else if(frame.ctrl == 0x07U){
    if(memcmp(frame.data, "LIGHT", 5U) == 0){
      cycleLight();
    }
  }else if(frame.ctrl == 0x08U){
    if(memcmp(frame.data, "MODE", 4U) == 0){
      cycleMode();
    }
  }
}

static void handleAppInput(void){
  uint32_t appValue;
  uint8_t appKey;

  appValue = remoteInfo.remoteVar_RX[0].var_uint32;
  if(appValue != APP_REMOTE_INVALID_U32){
    if(appValue <= VOLUME_MAX){
      doSetVolume((uint8_t)appValue);
    }
    remoteInfo.remoteVar_RX[0].var_uint32 = APP_REMOTE_INVALID_U32;
  }

  appValue = remoteInfo.remoteVar_RX[1].var_uint32;
  if(appValue != APP_REMOTE_INVALID_U32){
    if(appValue <= LIGHT_MAX){
      doSetLight((uint8_t)appValue);
    }
    remoteInfo.remoteVar_RX[1].var_uint32 = APP_REMOTE_INVALID_U32;
  }

  appKey = (uint8_t)(remoteInfo.remoteVar_RX[2].var_uint32 & 0xFFU);
  if((appKey == 1U) && (user1TaskInfo.appKeyPrev[0] == 0U)){
    doPlayPause();
  }
  user1TaskInfo.appKeyPrev[0] = appKey;

  appKey = (uint8_t)(remoteInfo.remoteVar_RX[3].var_uint32 & 0xFFU);
  if((appKey == 1U) && (user1TaskInfo.appKeyPrev[1] == 0U)){
    doNext();
  }
  user1TaskInfo.appKeyPrev[1] = appKey;

  appValue = remoteInfo.remoteVar_RX[4].var_uint32;
  if(appValue != APP_REMOTE_INVALID_U32){
    if(isValidAppTimerSetting(appValue) != 0U){
      doSetTimer((uint8_t)appValue);
    }
    remoteInfo.remoteVar_RX[4].var_uint32 = APP_REMOTE_INVALID_U32;
  }

  appKey = (uint8_t)(remoteInfo.remoteVar_RX[6].var_uint32 & 0xFFU);
  if((appKey == 1U) && (user1TaskInfo.appKeyPrev[2] == 0U)){
    cycleMode();
  }
  user1TaskInfo.appKeyPrev[2] = appKey;

  appKey = (uint8_t)(remoteInfo.remoteVar_RX[7].var_uint32 & 0xFFU);
  if((appKey == 1U) && (user1TaskInfo.appKeyPrev[3] == 0U)){
    doPrev();
  }
  user1TaskInfo.appKeyPrev[3] = appKey;
}

static void updateScreenDisplay(void){
  char buf[16];
  int len;
  const screenText_t *textInfo;

  if(user1TaskInfo.dirtyTrack != 0U){
    textInfo = &trackNameTable[user1TaskInfo.currentTrack - 1U];
    DRIVER_TJCLCD_SendText("t3", textInfo->data, textInfo->len);
    user1TaskInfo.dirtyTrack = 0U;
  }

  if(user1TaskInfo.dirtyVolume != 0U){
    len = snprintf(buf, sizeof(buf), "%u", (unsigned int)user1TaskInfo.volume);
    if(len > 0){
      DRIVER_TJCLCD_SendText("t10", (uint8_t *)buf, (uint8_t)len);
    }
    user1TaskInfo.dirtyVolume = 0U;
  }

  if(user1TaskInfo.dirtyTimer != 0U){
    if(user1TaskInfo.timerSetting == TIMER_OFF){
      DRIVER_TJCLCD_SendText("t4", textOff, sizeof(textOff));
    }else if(user1TaskInfo.timerRemainSec < 60U){
      len = snprintf(buf, sizeof(buf), "%lus", (unsigned long)user1TaskInfo.timerRemainSec);
      if(len > 0){
        DRIVER_TJCLCD_SendText("t4", (uint8_t *)buf, (uint8_t)len);
      }
    }else{
      len = snprintf(buf, sizeof(buf), "%lum", (unsigned long)(user1TaskInfo.timerRemainSec / 60U));
      if(len > 0){
        DRIVER_TJCLCD_SendText("t4", (uint8_t *)buf, (uint8_t)len);
      }
    }
    user1TaskInfo.dirtyTimer = 0U;
  }

  if(user1TaskInfo.dirtyLight != 0U){
    if(user1TaskInfo.lightBrightness == LIGHT_MIN){
      DRIVER_TJCLCD_SendText("t5", textOff, sizeof(textOff));
    }else{
      len = snprintf(buf, sizeof(buf), "%u%%", (unsigned int)user1TaskInfo.lightBrightness);
      if(len > 0){
        DRIVER_TJCLCD_SendText("t5", (uint8_t *)buf, (uint8_t)len);
      }
    }
    user1TaskInfo.dirtyLight = 0U;
  }

  if(user1TaskInfo.dirtyMode != 0U){
    textInfo = &modeNameTable[user1TaskInfo.currentMode];
    DRIVER_TJCLCD_SendText("t7", textInfo->data, textInfo->len);
    user1TaskInfo.dirtyMode = 0U;
  }
}

static void updateAppTxVars(void){
  memset(remoteInfo.remoteVar_TX, 0, sizeof(remoteInfo.remoteVar_TX));

  remoteInfo.remoteVar_TX[0].var_uint32  = user1TaskInfo.timerRemainSec;
  remoteInfo.remoteVar_TX[2].var_uint32  = (uint32_t)user1TaskInfo.currentTrack;
  remoteInfo.remoteVar_TX[3].var_uint32  = (uint32_t)user1TaskInfo.playState;
  remoteInfo.remoteVar_TX[6].var_uint32  = (uint32_t)user1TaskInfo.volume;
  remoteInfo.remoteVar_TX[7].var_uint32  = (uint32_t)((uint8_t)user1TaskInfo.currentMode + 1U);
  remoteInfo.remoteVar_TX[10].var_uint32 = (uint32_t)user1TaskInfo.lightBrightness;
  remoteInfo.remoteVar_TX[11].var_uint32 = (uint32_t)user1TaskInfo.timerSetting;
}

static void handleTimerCountdown(void){
  if(user1TaskInfo.timerState != TIMER_STATE_RUNNING) return;

  if(user1TaskInfo.timerRemainSec > 0U){
    user1TaskInfo.timerRemainSec--;
    user1TaskInfo.dirtyTimer = 1U;
  }

  if(user1TaskInfo.timerRemainSec == 0U){
    user1TaskInfo.timerState = TIMER_STATE_EXPIRED;

    DRIVER_MP3_Pause();
    user1TaskInfo.playState = PLAY_STATE_PAUSED;

    doSetLight(LIGHT_MIN);

    user1TaskInfo.timerState = TIMER_STATE_IDLE;
    user1TaskInfo.timerSetting = TIMER_OFF;
    user1TaskInfo.dirtyTimer = 1U;
  }
}

void user1TaskInit(void){
  osDelay(SYSTEM_READY_DELAY_MS);

  DRIVER_BLE_Init();
  DRIVER_TJCLCD_Init();
  DRIVER_WS2812_Init();
  DRIVER_WS2812_SetCtrlGpio(LIGHT_WS2812_CH0, LIGHT_WS2812_GPIO0);
  DRIVER_WS2812_SetCtrlGpio(LIGHT_WS2812_CH1, LIGHT_WS2812_GPIO1);
  DRIVER_MP3_Init();

  memset(&user1TaskInfo, 0, sizeof(user1TaskInfo));
  user1TaskInfo.currentTrack = 1U;
  user1TaskInfo.volume = VOLUME_DEFAULT;
  user1TaskInfo.playState = PLAY_STATE_STOPPED;
  user1TaskInfo.timerState = TIMER_STATE_IDLE;
  user1TaskInfo.timerSetting = TIMER_OFF;
  user1TaskInfo.timerRemainSec = 0U;
  user1TaskInfo.lightBrightness = LIGHT_DEFAULT;
  user1TaskInfo.currentMode = MODE_SLEEP;
  remoteInfo.remoteVar_RX[0].var_uint32 = APP_REMOTE_INVALID_U32;
  remoteInfo.remoteVar_RX[1].var_uint32 = APP_REMOTE_INVALID_U32;
  remoteInfo.remoteVar_RX[4].var_uint32 = APP_REMOTE_INVALID_U32;
  currentLightMode = scenePresetTable[MODE_SLEEP].lightMode;
  setAllDirty();

  DRIVER_MP3_SetVolume(getMp3Volume(VOLUME_DEFAULT));
  refreshWs2812Light();
  STDLIB_TIM_PwmSetDuty(LIGHT_PWM_CH, (float)LIGHT_DEFAULT);
}

void user1TaskUpdata(void *argument){
  (void)argument;

  user1TaskInit();

  for(;;){
    user1TaskInfo.taskCnt++;

    if((user1TaskInfo.taskCnt % LIGHT_EFFECT_BREATH_TICK) == 0U){
      handleScreenInput();
      updateWs2812BreathEffect();
    }

    if((user1TaskInfo.taskCnt % 25U) == 0U){
      handleAppInput();
    }

    if((user1TaskInfo.taskCnt % 50U) == 0U){
      updateScreenDisplay();
      updateAppTxVars();
    }

    if((user1TaskInfo.taskCnt % 500U) == 0U){
      handleTimerCountdown();
    }

    osDelay(2);
  }
}
