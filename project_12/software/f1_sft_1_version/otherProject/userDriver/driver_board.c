#include "driver_board.h"

/*============================================================================
 * 内部配置（仅driver_board模块内部使用）
 *============================================================================*/

/* 按键私有追踪状态 */
typedef struct {
    uint8_t  lastPressed;      /* 上次是否处于按下状态 */
    uint32_t pressStartTickMs; /* 按下起始时刻 */
    uint8_t  longPressCounted; /* 本次按下是否已触发长按计数（KeyInfoUpdate专用） */
} boardKeyState_t;

/* 按键私有状态缓存 */
static boardKeyState_t boardKeyState[BOARD_KEY_COUNT];
/* 按键信息缓存（向上提供） */
boardKeyInfo_t boardKeyInfoCache[BOARD_KEY_COUNT];
/* RGB闪烁计时缓存 */
static uint32_t boardRgbLastToggleTickMs[BOARD_RGB_COLOR_COUNT];

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
    uint32_t nowTickMs;

    STDLIB_COMMON_PeriphInit();
    DRIVER_BOARD_RgbOff(BOARD_RGB_R);
    DRIVER_BOARD_RgbOff(BOARD_RGB_G);

    DRIVER_BOARD_KeyInit();

    nowTickMs = STDLIB_COMMON_GetTickMs();
    for(uint8_t i = 0U; i < BOARD_RGB_COLOR_COUNT; i++){
        boardRgbLastToggleTickMs[i] = nowTickMs;
    }
}

/* 打开指定颜色RGB灯（低电平点亮） */
void DRIVER_BOARD_RgbOn(uint8_t color){
    STDLIB_COMMON_GpioWrite(boardRgbGpioMap[color], BOARD_DEP_RGB_ON_LEVEL);
}

/* 关闭指定颜色RGB灯 */
void DRIVER_BOARD_RgbOff(uint8_t color){
    STDLIB_COMMON_GpioWrite(boardRgbGpioMap[color], BOARD_DEP_RGB_OFF_LEVEL);
}

/* 非阻塞闪烁：周期调用，达到间隔后翻转一次 */
void DRIVER_BOARD_RgbBlink(uint8_t color, uint32_t intervalMs){
    uint8_t gpioId;
    uint8_t idx;
    uint32_t nowTickMs;

    gpioId = boardRgbGpioMap[color];
    idx = color;

    if(intervalMs == 0U){
        STDLIB_COMMON_GpioToggle(gpioId);
        return;
    }

    nowTickMs = STDLIB_COMMON_GetTickMs();
    if(__DRIVER_BOARD_ElapsedMs(nowTickMs, boardRgbLastToggleTickMs[idx]) < intervalMs) return;

    boardRgbLastToggleTickMs[idx] = nowTickMs;
    STDLIB_COMMON_GpioToggle(gpioId);
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

        boardKeyInfoCache[i].isPressed      = 0U;
        boardKeyInfoCache[i].isLongPressed  = 0U;
        boardKeyInfoCache[i].pressCount     = 0U;
        boardKeyInfoCache[i].longPressCount = 0U;
    }
}

/* 更新所有按键信息，检测按下与长按事件并累计计数（需周期调用） */
void DRIVER_BOARD_KeyInfoUpdate(void){
    uint8_t isPressed;
    uint32_t nowTickMs = STDLIB_COMMON_GetTickMs();

    for(uint8_t i = 0U; i < BOARD_KEY_COUNT; i++){
        isPressed = DRIVER_BOARD_KeyIsPressed(i);

        if(isPressed != 0U){
            boardKeyInfoCache[i].isPressed = 1U;

            /* 检测按下上升沿，累计按下次数 */
            if(boardKeyState[i].lastPressed == 0U){
                boardKeyState[i].lastPressed      = 1U;
                boardKeyState[i].pressStartTickMs = nowTickMs;
                boardKeyState[i].longPressCounted = 0U;
                boardKeyInfoCache[i].pressCount++;
            }

            /* 达到长按阈值且本次尚未计过，累计长按次数 */
            if((boardKeyState[i].longPressCounted == 0U) &&
               (__DRIVER_BOARD_ElapsedMs(nowTickMs, boardKeyState[i].pressStartTickMs) >= BOARD_KEY_LONG_PRESS_DEFAULT_MS)){
                boardKeyState[i].longPressCounted  = 1U;
                boardKeyInfoCache[i].isLongPressed = 1U;
                boardKeyInfoCache[i].longPressCount++;
            }
        } else {
            /* 按键释放，清除当次状态 */
            boardKeyInfoCache[i].isPressed     = 0U;
            boardKeyInfoCache[i].isLongPressed = 0U;
            boardKeyState[i].lastPressed       = 0U;
            boardKeyState[i].longPressCounted  = 0U;
        }
    }
}

/* 获取指定按键的信息结构体快照 */
uint8_t DRIVER_BOARD_KeyInfoGet(uint8_t keyId, boardKeyInfo_t *info){
    if(keyId >= BOARD_KEY_COUNT) return 0U;
    if(info == NULL) return 0U;
    *info = boardKeyInfoCache[keyId];
    return 1U;
}
