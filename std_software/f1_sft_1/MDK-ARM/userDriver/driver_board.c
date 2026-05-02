/*============================================================================
 * README
 *
 *============================================================================*/

#include "driver_board.h"

/*============================================================================
 * 内部配置（仅driver_board模块内部使用）
 *============================================================================*/

/* 按键私有追踪状态 */
typedef struct {
    uint8_t  lastPressed;       /* 上次是否处于按下状态 */
    uint32_t pressStartTickMs;  /* 按下起始时刻 */
    uint8_t  debounceConfirmed; /* 短按消抖是否已确认（持续按下≥BOARD_KEY_SHORT_PRESS_DEBOUNCE_MS） */
    uint8_t  longPressCounted;  /* 本次按下是否已触发长按计数（KeyInfoUpdate专用） */
} boardKeyState_t;

/* 按键私有状态缓存 */
static boardKeyState_t boardKeyState[BOARD_KEY_COUNT];
/* 开发板模块数据 */
boardInfo_t boardInfo;
/* 蜂鸣器开始计时时刻（ms） */
static uint32_t boardBuzzStartTickMs;
/* 蜂鸣器当前是否处于激活状态 */
static uint8_t  boardBuzzActive;

/* 按键ID到GPIO映射 */
static const uint8_t boardKeyGpioMap[BOARD_KEY_COUNT] = {
    BOARD_DEP_KEY1_GPIO_ID,
    BOARD_DEP_KEY2_GPIO_ID,
    BOARD_DEP_KEY3_GPIO_ID,
};
/* RGB颜色到GPIO映射 */
static const uint8_t boardRgbGpioMap[BOARD_RGB_COLOR_COUNT] = {
    BOARD_DEP_RGB_R_GPIO_ID,
    BOARD_DEP_RGB_G_GPIO_ID
};

/* 计算毫秒时间差（支持tick溢出） */
static uint32_t __DRIVER_BOARD_ElapsedMs(uint32_t nowTickMs, uint32_t startTickMs){
    return (nowTickMs - startTickMs);
}


/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化板级驱动 */
void DRIVER_BOARD_Init(void){
    STDLIB_COMMON_PeriphInit();
    DRIVER_BOARD_RgbSet(BOARD_RGB_R, 0U);
    DRIVER_BOARD_RgbSet(BOARD_RGB_G, 1U);

    DRIVER_BOARD_KeyInit();

    /* 初始化蜂鸣器：停止输出，清空计时状态 */
    boardInfo.buzzTimeMs    = 0U;
    boardBuzzActive         = 0U;
    boardBuzzStartTickMs    = 0U;
    STDLIB_TIM_PwmSetDuty(BOARD_DEP_BUZZER_PWM_CH, 0.0f);
}

/* 设置指定颜色RGB灯亮灭（isOn=1点亮，isOn=0熄灭） */
void DRIVER_BOARD_RgbSet(uint8_t color, uint8_t isOn){
    uint8_t level = (isOn != 0U) ? BOARD_DEP_RGB_ON_LEVEL : BOARD_DEP_RGB_OFF_LEVEL;
    STDLIB_COMMON_GpioWrite(boardRgbGpioMap[color], level);
}

/* 闪烁指定颜色RGB灯，每次调用翻转一次，闪烁周期由调用频率决定（需周期调用） */
void DRIVER_BOARD_RgbBlink(uint8_t color){
    STDLIB_COMMON_GpioToggle(boardRgbGpioMap[color]);
}

/* 读取按键IO电平 */
uint8_t DRIVER_BOARD_KeyRead(uint8_t keyId){
    return STDLIB_COMMON_GpioRead(boardKeyGpioMap[keyId]);
}

/* 判断按键是否按下（低电平按下） */
uint8_t DRIVER_BOARD_KeyIsPressed(uint8_t keyId){
    if(DRIVER_BOARD_KeyRead(keyId) == BOARD_DEP_KEY_PRESSED_LEVEL) return 1U;
    return 0U;
}

/* 判断按键是否长按（阈值为0时使用默认值1000ms） */
uint8_t DRIVER_BOARD_KeyIsLongPressed(uint8_t keyId, uint32_t longPressMs){
    uint8_t idx;
    uint8_t isPressed;
    uint32_t nowTickMs;

    if(longPressMs == 0U) longPressMs = BOARD_KEY_LONG_PRESS_DEFAULT_MS;

    idx = keyId;
    nowTickMs = STDLIB_COMMON_GetTickMs();
    isPressed = DRIVER_BOARD_KeyIsPressed(keyId);

    if(isPressed != 0U){
        if(boardKeyState[idx].lastPressed == 0U){
            boardKeyState[idx].lastPressed = 1U;
            boardKeyState[idx].pressStartTickMs = nowTickMs;
            return 0U;
        }

        if(__DRIVER_BOARD_ElapsedMs(nowTickMs, boardKeyState[idx].pressStartTickMs) >= longPressMs){
            return 1U;
        }
        return 0U;
    }

    boardKeyState[idx].lastPressed = 0U;
    boardKeyState[idx].pressStartTickMs = nowTickMs;
    return 0U;
}

/* 初始化所有按键信息，将状态置为默认高电平（未按下） */
void DRIVER_BOARD_KeyInit(void){
    uint32_t nowTickMs = STDLIB_COMMON_GetTickMs();

    for(uint8_t i = 0U; i < BOARD_KEY_COUNT; i++){
        boardKeyState[i].lastPressed      = 0U;
        boardKeyState[i].pressStartTickMs = nowTickMs;
        boardKeyState[i].longPressCounted = 0U;

        boardKeyState[i].debounceConfirmed = 0U;

        boardInfo.key[i].isPressed      = 0U;
        boardInfo.key[i].isLongPressed  = 0U;
        boardInfo.key[i].pressCount     = 0U;
        boardInfo.key[i].longPressCount = 0U;
    }
}

/* 更新所有按键信息，检测按下与长按事件并累计计数（需周期调用） */
void DRIVER_BOARD_KeyInfoUpdate(void){
    uint8_t isPressed;
    uint32_t nowTickMs = STDLIB_COMMON_GetTickMs();

    for(uint8_t i = 0U; i < BOARD_KEY_COUNT; i++){
        isPressed = DRIVER_BOARD_KeyIsPressed(i);

        if(isPressed != 0U){
            boardInfo.key[i].isPressed = 1U;

            /* 检测按下上升沿，开始消抖计时 */
            if(boardKeyState[i].lastPressed == 0U){
                boardKeyState[i].lastPressed       = 1U;
                boardKeyState[i].pressStartTickMs  = nowTickMs;
                boardKeyState[i].debounceConfirmed = 0U;
                boardKeyState[i].longPressCounted  = 0U;
            }

            /* 短按消抖：持续按下≥BOARD_KEY_SHORT_PRESS_DEBOUNCE_MS后确认一次短按 */
            if((boardKeyState[i].debounceConfirmed == 0U) &&
               (__DRIVER_BOARD_ElapsedMs(nowTickMs, boardKeyState[i].pressStartTickMs) >= BOARD_KEY_SHORT_PRESS_DEBOUNCE_MS)){
                boardKeyState[i].debounceConfirmed = 1U;
                boardInfo.key[i].pressCount++;
            }

            /* 长按检测：消抖确认后继续计时，达到长按阈值计一次长按 */
            if((boardKeyState[i].debounceConfirmed != 0U) &&
               (boardKeyState[i].longPressCounted  == 0U) &&
               (__DRIVER_BOARD_ElapsedMs(nowTickMs, boardKeyState[i].pressStartTickMs) >= BOARD_KEY_LONG_PRESS_DEFAULT_MS)){
                boardKeyState[i].longPressCounted  = 1U;
                boardInfo.key[i].isLongPressed = 1U;
                boardInfo.key[i].longPressCount++;
            }
        } else {
            /* 按键释放，清除当次状态 */
            boardInfo.key[i].isPressed         = 0U;
            boardInfo.key[i].isLongPressed     = 0U;
            boardKeyState[i].lastPressed       = 0U;
            boardKeyState[i].debounceConfirmed = 0U;
            boardKeyState[i].longPressCounted  = 0U;
        }
    }
}

/* 获取指定按键的信息结构体快照 */
uint8_t DRIVER_BOARD_KeyInfoGet(uint8_t keyId, boardKeyInfo_t *info){
    if(keyId >= BOARD_KEY_COUNT) return 0U;
    if(info == NULL) return 0U;
    *info = boardInfo.key[keyId];
    return 1U;
}

/* 蜂鸣器非阻塞更新，需在10ms周期内调用。
 * 读取 boardInfo.buzzTimeMs：
 *   0       → 立即停止蜂鸣
 *   0xFFFF  → 持续蜂鸣（不自动停止）
 *   其它值  → 蜂鸣指定毫秒后自动停止，并将 buzzTimeMs 置0
 */
void DRIVER_BOARD_BuzzUpdate(void){
  uint32_t nowTickMs;

  if(boardInfo.buzzTimeMs == 0U){
    /* 停止蜂鸣 */
    if(boardBuzzActive != 0U){
      STDLIB_TIM_PwmSetDuty(BOARD_DEP_BUZZER_PWM_CH, 0.0f);
      boardBuzzActive = 0U;
    }
    return;
  }

  if(boardInfo.buzzTimeMs == 0xFFFFU){
    /* 持续蜂鸣 */
    if(boardBuzzActive == 0U){
      STDLIB_TIM_PwmSetDuty(BOARD_DEP_BUZZER_PWM_CH, 100.0f);
      boardBuzzActive = 1U;
    }
    return;
  }

  /* 定时蜂鸣 */
  if(boardBuzzActive == 0U){
    /* 新蜂鸣请求：启动计时，开始蜂鸣 */
    boardBuzzStartTickMs = STDLIB_COMMON_GetTickMs();
    STDLIB_TIM_PwmSetDuty(BOARD_DEP_BUZZER_PWM_CH, 100.0f);
    boardBuzzActive = 1U;
    return;
  }

  /* 已在蜂鸣中，检查是否到达设定时长 */
  nowTickMs = STDLIB_COMMON_GetTickMs();
  if((nowTickMs - boardBuzzStartTickMs) >= (uint32_t)boardInfo.buzzTimeMs){
    STDLIB_TIM_PwmSetDuty(BOARD_DEP_BUZZER_PWM_CH, 0.0f);
    boardInfo.buzzTimeMs = 0U;
    boardBuzzActive      = 0U;
  }
}
