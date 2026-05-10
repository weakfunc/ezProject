#ifndef __DRIVER_BOARD_H__
#define __DRIVER_BOARD_H__

#include <stdint.h>
#include "stdlib_common.h"
#include "stdlib_tim.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 *============================================================================*/
#define BOARD_DEP_BUZZER_PWM_CH              PWM_TIM4_CH1
#define BOARD_DEP_KEY1_GPIO_ID               GPIO_ID_KEY_1
#define BOARD_DEP_KEY2_GPIO_ID               GPIO_ID_KEY_2
#define BOARD_DEP_KEY3_GPIO_ID               GPIO_ID_KEY_3
#define BOARD_DEP_RGB_R_GPIO_ID              GPIO_ID_USER_IO_8
#define BOARD_DEP_RGB_G_GPIO_ID              GPIO_ID_USER_IO_7
#define BOARD_DEP_KEY_RELEASE_LEVEL          GPIO_LEVEL_HIGH
#define BOARD_DEP_KEY_PRESSED_LEVEL          GPIO_LEVEL_LOW
#define BOARD_DEP_RGB_ON_LEVEL               GPIO_LEVEL_LOW
#define BOARD_DEP_RGB_OFF_LEVEL              GPIO_LEVEL_HIGH

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/
#define BOARD_KEY1                           (0U)
#define BOARD_KEY2                           (1U)
#define BOARD_KEY3                           (2U)
#define BOARD_RGB_G                          (0U)
#define BOARD_RGB_R                          (1U)
#define BOARD_KEY_COUNT                      (3U)
#define BOARD_RGB_COLOR_COUNT                (2U)
#define BOARD_KEY_SHORT_PRESS_DEBOUNCE_MS    (50U)
#define BOARD_KEY_LONG_PRESS_DEFAULT_MS      (1000U)

/* 按键信息管理结构体 */
typedef struct {
    uint8_t  isPressed;      /* 当前是否按下 */
    uint8_t  isLongPressed;  /* 当前是否处于长按状态 */
    uint32_t pressCount;     /* 累计按下次数 */
    uint32_t lastPressCount;     /* 累计按下次数 */
    uint32_t longPressCount; /* 累计长按次数 */
} boardKeyInfo_t;

/* 开发板模块信息结构体 */
typedef struct {
  boardKeyInfo_t key[BOARD_KEY_COUNT]; /* 各按键信息 */
  uint16_t       buzzTimeMs;           /* 蜂鸣时长（ms），0=停止，0xFFFF=持续蜂鸣 */
} boardInfo_t;

extern boardInfo_t boardInfo;

/* 开发板驱动初始化 */
void DRIVER_BOARD_Init(void);

/* 设置指定颜色RGB灯亮灭（isOn=1点亮，isOn=0熄灭） */
void DRIVER_BOARD_RgbSet(uint8_t color, uint8_t isOn);

/* 闪烁指定颜色RGB灯，每次调用翻转一次，闪烁周期由调用频率决定（需周期调用） */
void DRIVER_BOARD_RgbBlink(uint8_t color);

/* 读取按键输入电平 */
uint8_t DRIVER_BOARD_KeyRead(uint8_t keyId);

/* 判断按键是否按下 */
uint8_t DRIVER_BOARD_KeyIsPressed(uint8_t keyId);

/* 判断按键是否长按（longPressMs=0时用默认1000ms） */
uint8_t DRIVER_BOARD_KeyIsLongPressed(uint8_t keyId, uint32_t longPressMs);

/* 初始化所有按键信息，将状态置为默认高电平（未按下） */
void DRIVER_BOARD_KeyInit(void);

/* 更新所有按键信息，检测按下与长按事件并累计计数（需周期调用） */
void DRIVER_BOARD_KeyInfoUpdate(void);

/* 获取指定按键的信息结构体快照，成功返回1，keyId越界返回0 */
uint8_t DRIVER_BOARD_KeyInfoGet(uint8_t keyId, boardKeyInfo_t *info);

/* 蜂鸣器非阻塞更新，需在10ms周期内调用；读取boardInfo.buzzTimeMs决定蜂鸣状态 */
void DRIVER_BOARD_BuzzUpdate(void);

#endif
