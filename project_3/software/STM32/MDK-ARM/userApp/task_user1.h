#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"

/* 音频参数 */
#define TRACK_COUNT         15U
#define VOLUME_DEFAULT      50U
#define VOLUME_MAX          100U
#define VOLUME_MIN          0U
#define VOLUME_STEP         5U

/* 定时选项，单位：分钟 */
#define TIMER_OFF           0U
#define TIMER_1MIN          1U
#define TIMER_5MIN          5U
#define TIMER_10MIN         10U
#define TIMER_15MIN         15U
#define TIMER_30MIN         30U
#define TIMER_60MIN         60U

/* 夜灯参数 */
#define LIGHT_DEFAULT       50U
#define LIGHT_MAX           100U
#define LIGHT_MIN           0U

/* 场景模式 */
typedef enum {
  MODE_SLEEP = 0,
  MODE_FOCUS,
  MODE_MEDITATE,
  MODE_BABY,
  MODE_COUNT
} sceneMode_e;

/* 播放状态 */
typedef enum {
  PLAY_STATE_STOPPED = 0,
  PLAY_STATE_PLAYING,
  PLAY_STATE_PAUSED
} playState_e;

/* 定时状态 */
typedef enum {
  TIMER_STATE_IDLE = 0,
  TIMER_STATE_RUNNING,
  TIMER_STATE_EXPIRED
} timerState_e;

typedef struct {
  uint32_t taskCnt;

  /* 音频状态 */
  uint8_t     currentTrack;
  uint8_t     volume;
  playState_e playState;

  /* 定时状态 */
  timerState_e timerState;
  uint8_t      timerSetting;
  uint32_t     timerRemainSec;

  /* 灯光状态 */
  uint8_t lightBrightness;

  /* 场景模式 */
  sceneMode_e currentMode;

  /* APP key edge-detect state */
  uint8_t appKeyPrev[4];

  /* 串口屏脏标志 */
  uint8_t dirtyTrack;
  uint8_t dirtyVolume;
  uint8_t dirtyTimer;
  uint8_t dirtyLight;
  uint8_t dirtyMode;
} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;

void user1TaskInit(void);
void user1TaskUpdata(void *argument);

#endif
