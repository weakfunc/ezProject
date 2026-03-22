#ifndef __DRIVER_WS2812_H__
#define __DRIVER_WS2812_H__

#include <stdint.h>
#include "stdlib_common.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 *============================================================================*/
#define WS2812_DEP_CTRL_GPIO_DEFAULT                        GPIO_ID_NULL
#define WS2812_DEP_GPIO_LEVEL_LOW                           GPIO_LEVEL_LOW
#define WS2812_DEP_GPIO_LEVEL_HIGH                          GPIO_LEVEL_HIGH
#define WS2812_DEP_GPIO_GET_PIN_INFO(gpioId, pinInfo)       STDLIB_COMMON_GpioGetPinInfo((gpioId), (pinInfo))
#define WS2812_DEP_GPIO_WRITE(gpioId, level)                STDLIB_COMMON_GpioWrite((gpioId), (level))
#define WS2812_DEP_CRITICAL_ENTER()                         STDLIB_COMMON_EnterCritical()
#define WS2812_DEP_CRITICAL_EXIT(primask)                   STDLIB_COMMON_ExitCritical((primask))
#define WS2812_DEP_DELAY_CYCLE(cycleCount)                  __STDLIB_COMMON_DelayCycle((cycleCount))

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/
#define WS2812_LED_MAX_COUNT                  (64U)

/* WS2812单灯珠颜色数据 */
typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} ws2812Color_t;

/* 初始化WS2812驱动 */
void DRIVER_WS2812_Init(void);

/* 设置WS2812控制引脚，成功返回1 */
uint8_t DRIVER_WS2812_SetCtrlGpio(uint8_t gpioId);

/* 设置指定灯珠RGB颜色 */
void DRIVER_WS2812_SetColor(uint16_t ledId, uint8_t red, uint8_t green, uint8_t blue);

/* 设置全部灯珠为同一RGB颜色 */
void DRIVER_WS2812_SetAllColor(uint8_t red, uint8_t green, uint8_t blue);

/* 清空全部灯珠缓存颜色 */
void DRIVER_WS2812_Clear(void);

/* 刷新前ledCount个级联灯珠 */
void DRIVER_WS2812_Refresh(uint16_t ledCount);

#endif
