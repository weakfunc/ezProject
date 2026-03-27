/*******************************************************************************
 * @file    task_user1.c
 * @brief   应用层主任务：串口屏交互、MP3音频播放、WS2812灯光控制、定时关闭
 ******************************************************************************/

#include "task_user1.h"
#include "driver_board.h"
#include "driver_ws2812.h"
#include "driver_mp3.h"
#include "driver_tjcLCD.h"
#include <string.h>

user1TaskInfo_t user1TaskInfo;

/*============================================================================
 * 内部配置
 *============================================================================*/

/* 曲目总数 */
#define APP_TRACK_COUNT       (15U)
/* 定时选项总数（关/5m/10m/15m/30m/60m） */
#define APP_TIMER_OPT_COUNT   (6U)
/* 灯光颜色选项总数（关/暖白/红/橙/黄/绿/青/蓝/紫/粉） */
#define APP_LIGHT_OPT_COUNT   (10U)
/* 模式总数（助眠/专注/放松/冥想/活力/夜眠） */
#define APP_MODE_COUNT        (6U)
/* WS2812通道配置（通道0：USER_IO_1，通道1：USER_IO_2，每通道16颗灯珠） */
#define APP_WS2812_CH0        (WS2812_CH_0)
#define APP_WS2812_CH1        (WS2812_CH_1)
#define APP_WS2812_GPIO0      (GPIO_ID_USER_IO_1)
#define APP_WS2812_GPIO1      (GPIO_ID_USER_IO_2)
#define APP_WS2812_LED_CNT    (16U)
/* 呼吸灯步进（值越大呼吸越快，1~255） */
#define APP_BREATH_STEP       (3U)
/* 任务周期（ms） */
#define APP_TASK_PERIOD_MS    (10U)
/* 默认音量（MP3模块0~30级） */
#define APP_VOL_DEFAULT       (20U)
/* 每次音量调节步进 */
#define APP_VOL_STEP          (2U)

/*============================================================================
 * 曲目名称（GB2312编码字节数组，共15首）
 *============================================================================*/

static const uint8_t trackStr0[]  = {0xD3,0xEA,0xC9,0xF9};  /* 雨声 */
static const uint8_t trackStr1[]  = {0xBA,0xA3,0xC0,0xCB};  /* 海浪 */
static const uint8_t trackStr2[]  = {0xC9,0xAD,0xC1,0xD6};  /* 森林 */
static const uint8_t trackStr3[]  = {0xC1,0xF7,0xCB,0xAE};  /* 流水 */
static const uint8_t trackStr4[]  = {0xB7,0xE7,0xC9,0xF9};  /* 风声 */
static const uint8_t trackStr5[]  = {0xC0,0xD7,0xC9,0xF9};  /* 雷声 */
static const uint8_t trackStr6[]  = {0xB0,0xD7,0xD4,0xEB};  /* 白噪 */
static const uint8_t trackStr7[]  = {0xC4,0xF1,0xC3,0xF9};  /* 鸟鸣 */
static const uint8_t trackStr8[]  = {0xC0,0xD7,0xD3,0xEA};  /* 雷雨 */
static const uint8_t trackStr9[]  = {0xD2,0xB9,0xD3,0xEA};  /* 夜雨 */
static const uint8_t trackStr10[] = {0xBA,0xA3,0xB7,0xE7};  /* 海风 */
static const uint8_t trackStr11[] = {0xC1,0xD6,0xC4,0xF1};  /* 林鸟 */
static const uint8_t trackStr12[] = {0xCB,0xAE,0xC9,0xF9};  /* 水声 */
static const uint8_t trackStr13[] = {0xB7,0xE7,0xD3,0xEA};  /* 风雨 */
static const uint8_t trackStr14[] = {0xD2,0xB9,0xC9,0xF9};  /* 夜声 */

/* 曲目名称指针查找表 */
static const uint8_t * const trackStrTable[APP_TRACK_COUNT] = {
  trackStr0,  trackStr1,  trackStr2,  trackStr3,  trackStr4,
  trackStr5,  trackStr6,  trackStr7,  trackStr8,  trackStr9,
  trackStr10, trackStr11, trackStr12, trackStr13, trackStr14,
};

/* 曲目名称字节长度查找表 */
static const uint8_t trackStrLen[APP_TRACK_COUNT] = {
  4U, 4U, 4U, 4U, 4U, 4U, 4U, 4U, 4U, 4U, 4U, 4U, 4U, 4U, 4U,
};

/*============================================================================
 * 定时选项显示字符串（GB2312/ASCII混合）
 *============================================================================*/

static const uint8_t timerStr0[] = {0xB9,0xD8};             /* 关 */
static const uint8_t timerStr1[] = {'5','m'};                /* 5m */
static const uint8_t timerStr2[] = {'1','0','m'};            /* 10m */
static const uint8_t timerStr3[] = {'1','5','m'};            /* 15m */
static const uint8_t timerStr4[] = {'3','0','m'};            /* 30m */
static const uint8_t timerStr5[] = {'6','0','m'};            /* 60m */

static const uint8_t * const timerStrTable[APP_TIMER_OPT_COUNT] = {
  timerStr0, timerStr1, timerStr2, timerStr3, timerStr4, timerStr5,
};
static const uint8_t timerStrLen[APP_TIMER_OPT_COUNT] = {2U, 2U, 3U, 3U, 3U, 3U};

/* 各定时选项对应的倒计时时长（ms），0表示不定时 */
static const uint32_t timerDurationMs[APP_TIMER_OPT_COUNT] = {
  0U,
  5U  * 60U * 1000U,
  10U * 60U * 1000U,
  15U * 60U * 1000U,
  30U * 60U * 1000U,
  60U * 60U * 1000U,
};

/*============================================================================
 * 灯光颜色选项显示字符串及RGB值
 *============================================================================*/

static const uint8_t lightStr0[] = {0xB9,0xD8};             /* 关 */
static const uint8_t lightStr1[] = {0xC5,0xAF,0xB0,0xD7};  /* 暖白 */
static const uint8_t lightStr2[] = {0xBA,0xEC};             /* 红 */
static const uint8_t lightStr3[] = {0xB3,0xC8};             /* 橙 */
static const uint8_t lightStr4[] = {0xBB,0xC6};             /* 黄 */
static const uint8_t lightStr5[] = {0xC2,0xCC};             /* 绿 */
static const uint8_t lightStr6[] = {0xC7,0xE0};             /* 青 */
static const uint8_t lightStr7[] = {0xC0,0xB6};             /* 蓝 */
static const uint8_t lightStr8[] = {0xD7,0xCF};             /* 紫 */
static const uint8_t lightStr9[] = {0xB7,0xDB};             /* 粉 */

static const uint8_t * const lightStrTable[APP_LIGHT_OPT_COUNT] = {
  lightStr0, lightStr1, lightStr2, lightStr3, lightStr4,
  lightStr5, lightStr6, lightStr7, lightStr8, lightStr9,
};
static const uint8_t lightStrLen[APP_LIGHT_OPT_COUNT] = {
  2U, 4U, 2U, 2U, 2U, 2U, 2U, 2U, 2U, 2U,
};

/* 各灯光颜色选项对应的RGB值，顺序与lightStrTable一致
 * 注意：driver层SendColor按GRB顺序发送，此处填写RGB逻辑值 */
static const uint8_t lightRgb[APP_LIGHT_OPT_COUNT][3] = {
  {  0U,   0U,   0U},  /* 关   */
  {255U, 200U,  80U},  /* 暖白 */
  {255U,   0U,   0U},  /* 红   */
  {255U,  80U,   0U},  /* 橙   */
  {200U, 150U,   0U},  /* 黄   */
  {  0U, 200U,   0U},  /* 绿   */
  {  0U, 180U, 180U},  /* 青   */
  {  0U,   0U, 255U},  /* 蓝   */
  {180U,   0U, 200U},  /* 紫   */
  {255U,  30U, 120U},  /* 粉   */
};

/*============================================================================
 * 模式显示字符串及预设配置
 *============================================================================*/

static const uint8_t modeStr0[] = {0xD6,0xFA,0xC3,0xDF};             /* 助眠 */
static const uint8_t modeStr1[] = {0xD7,0xA8,0xD7,0xA2};             /* 专注 */
static const uint8_t modeStr2[] = {0xB7,0xC5,0xCB,0xC9};             /* 放松 */
static const uint8_t modeStr3[] = {0xDA,0xA4,0xCF,0xEB};             /* 冥想 */
static const uint8_t modeStr4[] = {0xBB,0xEE,0xC1,0xA6};             /* 活力 */
static const uint8_t modeStr5[] = {0xD2,0xB9,0xC3,0xDF};             /* 夜眠 */

static const uint8_t * const modeStrTable[APP_MODE_COUNT] = {
  modeStr0, modeStr1, modeStr2, modeStr3, modeStr4, modeStr5,
};
static const uint8_t modeStrLen[APP_MODE_COUNT] = {4U, 4U, 4U, 4U, 4U, 4U};

/* 各模式预设曲目索引（0-based） */
static const uint8_t modeDefaultTrack[APP_MODE_COUNT] = {
  0U,   /* 助眠：雨声 */
  6U,   /* 专注：白噪 */
  2U,   /* 放松：森林 */
  3U,   /* 冥想：流水 */
  7U,   /* 活力：鸟鸣 */
  14U,  /* 夜眠：夜声 */
};

/* 各模式预设灯光颜色索引（对应lightStrTable） */
static const uint8_t modeDefaultLight[APP_MODE_COUNT] = {
  1U,   /* 助眠：暖白 */
  5U,   /* 专注：绿   */
  7U,   /* 放松：蓝   */
  8U,   /* 冥想：紫   */
  2U,   /* 活力：红   */
  1U,   /* 夜眠：暖白 */
};

/* 各模式对应灯效类型（0=呼吸，1=常亮，2=流水） */
static const uint8_t modeEffectType[APP_MODE_COUNT] = {
  0U,   /* 助眠：呼吸 */
  1U,   /* 专注：常亮 */
  2U,   /* 放松：流水 */
  0U,   /* 冥想：呼吸 */
  2U,   /* 活力：流水 */
  1U,   /* 夜眠：常亮 */
};

/*============================================================================
 * 应用状态变量
 *============================================================================*/

/* 当前曲目索引（0~14） */
static uint8_t  appTrackIdx      = 0U;
/* 当前定时选项索引（0=关） */
static uint8_t  appTimerIdx      = 0U;
/* 当前灯光颜色索引（0=关） */
static uint8_t  appLightIdx      = 0U;
/* 当前模式索引（0=助眠/1=专注/2=放松/3=冥想/4=活力/5=夜眠） */
static uint8_t  appModeIdx       = 0U;
/* 是否正在播放音频 */
static uint8_t  appIsPlaying     = 0U;
/* 定时剩余毫秒（0表示未激活或已到期） */
static uint32_t appTimerMs       = 0U;
/* 当前音量（MP3模块0~30级） */
static uint8_t  appVolume        = APP_VOL_DEFAULT;
/* 串口屏刷新请求标志（置1时下次循环刷新） */
static uint8_t  appNeedLcdUpdate = 1U;
/* 任务节拍计数，每10ms自增一次，用于灯效时序 */
static uint32_t appTickCnt       = 0U;

/*============================================================================
 * 内部函数
 *============================================================================*/

/**
 * @brief  将0~100的整数转为ASCII字节串，用于串口屏音量显示
 * @param  val  数值（0~100）
 * @param  buf  输出缓冲区（至少3字节）
 * @param  len  输出实际字节数
 */
static void appU8ToStr(uint8_t val, uint8_t *buf, uint8_t *len)
{
  if(val >= 100U) {
    buf[0] = (uint8_t)'1'; buf[1] = (uint8_t)'0'; buf[2] = (uint8_t)'0';
    *len = 3U;
  } else if(val >= 10U) {
    buf[0] = (uint8_t)('0' + val / 10U);
    buf[1] = (uint8_t)('0' + val % 10U);
    *len = 2U;
  } else {
    buf[0] = (uint8_t)('0' + val);
    *len = 1U;
  }
}

/**
 * @brief  根据当前模式灯效类型和灯光颜色索引，配置并应用WS2812灯效
 *         效果类型由modeEffectType[appModeIdx]决定：0=呼吸，1=常亮，2=流水
 */
static void appApplyLight(void)
{
  uint8_t r = lightRgb[appLightIdx][0];
  uint8_t g = lightRgb[appLightIdx][1];
  uint8_t b = lightRgb[appLightIdx][2];

  if(appLightIdx == 0U) {
    /* 灯光关闭：两个通道均清空并刷新 */
    DRIVER_WS2812_Clear(APP_WS2812_CH0);
    DRIVER_WS2812_Refresh(APP_WS2812_CH0, APP_WS2812_LED_CNT);
    DRIVER_WS2812_Clear(APP_WS2812_CH1);
    DRIVER_WS2812_Refresh(APP_WS2812_CH1, APP_WS2812_LED_CNT);
    return;
  }

  switch(modeEffectType[appModeIdx]) {
    case 0U:
      /* 呼吸灯：两个通道均配置呼吸效果 */
      DRIVER_WS2812_BreathSetup(APP_WS2812_CH0, r, g, b, APP_WS2812_LED_CNT, APP_BREATH_STEP);
      DRIVER_WS2812_BreathSetup(APP_WS2812_CH1, r, g, b, APP_WS2812_LED_CNT, APP_BREATH_STEP);
      break;
    case 1U:
      /* 常亮：两个通道均直接刷新 */
      DRIVER_WS2812_SetAllColor(APP_WS2812_CH0, r, g, b);
      DRIVER_WS2812_Refresh(APP_WS2812_CH0, APP_WS2812_LED_CNT);
      DRIVER_WS2812_SetAllColor(APP_WS2812_CH1, r, g, b);
      DRIVER_WS2812_Refresh(APP_WS2812_CH1, APP_WS2812_LED_CNT);
      break;
    case 2U:
      /* 流水灯：两个通道均配置流水效果 */
      DRIVER_WS2812_ChaseSetup(APP_WS2812_CH0, r, g, b, APP_WS2812_LED_CNT);
      DRIVER_WS2812_ChaseSetup(APP_WS2812_CH1, r, g, b, APP_WS2812_LED_CNT);
      break;
    default:
      break;
  }
}

/**
 * @brief  周期性推进WS2812动态灯效（呼吸/流水），每次任务周期调用
 *         呼吸灯每20ms推进一帧，流水灯每100ms推进一步
 */
static void appUpdateLightEffect(void)
{
  uint8_t effect;

  if(appLightIdx == 0U) { return; }
  effect = modeEffectType[appModeIdx];

  /* 呼吸灯：每20ms（2个周期）推进一帧 */
  if((effect == 0U) && ((appTickCnt % 2U) == 0U)) {
    DRIVER_WS2812_BreathUpdate(APP_WS2812_CH0);
    DRIVER_WS2812_BreathUpdate(APP_WS2812_CH1);
  }
  /* 流水灯：每100ms（10个周期）推进一步 */
  if((effect == 2U) && ((appTickCnt % 10U) == 0U)) {
    DRIVER_WS2812_ChaseUpdate(APP_WS2812_CH0);
    DRIVER_WS2812_ChaseUpdate(APP_WS2812_CH1);
  }
}

/**
 * @brief  刷新串口屏五个文本控件（t3曲目/t4定时/t5灯光/t7模式/t10音量）
 */
static void appUpdateLcd(void)
{
  uint8_t volBuf[3];
  uint8_t volLen;
  uint8_t volPct;

  /* t3：当前曲目名 */
  DRIVER_TJCLCD_SendText("t3",
    trackStrTable[appTrackIdx], trackStrLen[appTrackIdx]);
  /* t4：当前定时状态 */
  DRIVER_TJCLCD_SendText("t4",
    timerStrTable[appTimerIdx], timerStrLen[appTimerIdx]);
  /* t5：当前灯光颜色 */
  DRIVER_TJCLCD_SendText("t5",
    lightStrTable[appLightIdx], lightStrLen[appLightIdx]);
  /* t7：当前模式 */
  DRIVER_TJCLCD_SendText("t7",
    modeStrTable[appModeIdx], modeStrLen[appModeIdx]);
  /* t10：当前音量百分比（MP3 0~30级映射为0~100%） */
  volPct = (uint8_t)(appVolume * 100U / 30U);
  appU8ToStr(volPct, volBuf, &volLen);
  DRIVER_TJCLCD_SendText("t10", volBuf, volLen);
}

/**
 * @brief  处理串口屏一帧按键事件
 * @param  frame  已接收并解析完成的串口屏数据帧指针
 *
 * 帧格式说明：
 *   ctrl=0x05：播放控制类（PREV/PLAY/NEXT），由data[1]区分
 *   ctrl=0x06：定时（TIME）
 *   ctrl=0x07：灯光（LIGHT）
 *   ctrl=0x08：模式（MODE）
 */
static void appHandleLcdFrame(const tjcLcdFrame_t *frame)
{
  uint8_t ctrl         = frame->ctrl;
  const uint8_t *d     = frame->data;

  if(ctrl == 0x05U) {
    /* 播放/音量控制类，通过data[1]区分：R=PREV, L=PLAY, E=NEXT, O=VOL+/VOL- */
    if(d[1] == (uint8_t)'R') {
      /* b0 PREV：切换到上一曲并播放 */
      appTrackIdx = (appTrackIdx == 0U) ?
        (APP_TRACK_COUNT - 1U) : (appTrackIdx - 1U);
      DRIVER_MP3_PlayTrack((uint16_t)appTrackIdx + 1U);
      appIsPlaying     = 1U;
      appNeedLcdUpdate = 1U;

    } else if(d[1] == (uint8_t)'L') {
      /* b1 PLAY：播放/暂停切换 */
      if(appIsPlaying != 0U) {
        DRIVER_MP3_Pause();
        appIsPlaying = 0U;
      } else {
        DRIVER_MP3_Play();
        appIsPlaying = 1U;
      }

    } else if(d[1] == (uint8_t)'E') {
      /* b2 NEXT：切换到下一曲并播放 */
      appTrackIdx = (appTrackIdx + 1U) % APP_TRACK_COUNT;
      DRIVER_MP3_PlayTrack((uint16_t)appTrackIdx + 1U);
      appIsPlaying     = 1U;
      appNeedLcdUpdate = 1U;

    } else if(d[1] == (uint8_t)'O') {
      /* b6/b7 VOL+/VOL-：通过data[3]区分'+'与'-' */
      if(d[3] == (uint8_t)'+') {
        /* b6 VOL+：音量增加，上限30 */
        if(appVolume + APP_VOL_STEP <= 30U) {
          appVolume += APP_VOL_STEP;
        } else {
          appVolume = 30U;
        }
      } else {
        /* b7 VOL-：音量减少，下限0 */
        if(appVolume >= APP_VOL_STEP) {
          appVolume -= APP_VOL_STEP;
        } else {
          appVolume = 0U;
        }
      }
      DRIVER_MP3_SetVolume(appVolume);
      appNeedLcdUpdate = 1U;
    }

  } else if(ctrl == 0x06U) {
    /* b3 TIME：循环切换定时选项并重置倒计时 */
    appTimerIdx      = (appTimerIdx + 1U) % APP_TIMER_OPT_COUNT;
    appTimerMs       = timerDurationMs[appTimerIdx];
    appNeedLcdUpdate = 1U;

  } else if(ctrl == 0x07U) {
    /* b4 LIGHT：循环切换灯光颜色并立即应用 */
    appLightIdx      = (appLightIdx + 1U) % APP_LIGHT_OPT_COUNT;
    appApplyLight();
    appNeedLcdUpdate = 1U;

  } else if(ctrl == 0x08U) {
    /* b5 MODE：循环切换模式，并应用该模式的曲目和灯光预设 */
    appModeIdx       = (appModeIdx + 1U) % APP_MODE_COUNT;
    appTrackIdx      = modeDefaultTrack[appModeIdx];
    appLightIdx      = modeDefaultLight[appModeIdx];
    DRIVER_MP3_PlayTrack((uint16_t)appTrackIdx + 1U);
    appIsPlaying     = 1U;
    appApplyLight();
    appNeedLcdUpdate = 1U;
  }
}

/*============================================================================
 * 任务初始化
 *============================================================================*/

void user1TaskInit(){
  /* 初始化WS2812，绑定两个通道控制引脚 */
  DRIVER_WS2812_Init();
  DRIVER_WS2812_SetCtrlGpio(APP_WS2812_CH0, APP_WS2812_GPIO0);
  DRIVER_WS2812_SetCtrlGpio(APP_WS2812_CH1, APP_WS2812_GPIO1);

  /* 初始化MP3模块（含2s启动延时，等待模块完成上电） */
  DRIVER_MP3_Init();
  DRIVER_MP3_SetVolume(APP_VOL_DEFAULT);

  /* 初始化TJC串口屏驱动（绑定USART3字节回调） */
  DRIVER_TJCLCD_Init();

  /* 应用默认模式（助眠）预设：暖白呼吸灯，不自动播放 */
  appTrackIdx = modeDefaultTrack[appModeIdx];
  appLightIdx = modeDefaultLight[appModeIdx];
  appApplyLight();

  /* 标记首次LCD刷新 */
  appNeedLcdUpdate = 1U;
}

/*============================================================================
 * 任务主循环（每10ms执行一次）
 *============================================================================*/

void user1TaskUpdata(void *argument){
  tjcLcdFrame_t rxFrame;

  user1TaskInit();

  for(;;) {
    user1TaskInfo.user1TaskCnt++;
    appTickCnt++;

    /* 处理串口屏按键事件 */
    if(DRIVER_TJCLCD_GetRxFrame(&rxFrame) != 0U) {
      appHandleLcdFrame(&rxFrame);
    }

    /* 定时关闭倒计时处理 */
    if((appTimerIdx != 0U) && (appTimerMs > 0U)) {
      if(appTimerMs > APP_TASK_PERIOD_MS) {
        appTimerMs -= APP_TASK_PERIOD_MS;
      } else {
        /* 定时到期：停止播放，关灯，清除定时 */
        appTimerMs       = 0U;
        appTimerIdx      = 0U;
        appLightIdx      = 0U;
        DRIVER_MP3_Pause();
        appIsPlaying     = 0U;
        appApplyLight();
        appNeedLcdUpdate = 1U;
      }
    }

    /* 推进WS2812动态灯效（呼吸灯/流水灯） */
    appUpdateLightEffect();

    /* 状态变化时刷新串口屏显示 */
    if(appNeedLcdUpdate != 0U) {
      appNeedLcdUpdate = 0U;
      appUpdateLcd();
    }

    osDelay(APP_TASK_PERIOD_MS);
  }
}
