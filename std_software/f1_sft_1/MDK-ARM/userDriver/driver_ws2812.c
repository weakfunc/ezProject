#include "driver_ws2812.h"
#include <string.h>

/*============================================================================
 * 内部配置（仅driver_ws2812模块内部使用）
 *============================================================================*/

/* 72MHz主频下按WS2812典型时序估算的短延时循环次数 */
static const uint16_t ws2812Bit0HighDelayCycle = 8U;
static const uint16_t ws2812Bit0LowDelayCycle = 18U;
static const uint16_t ws2812Bit1HighDelayCycle = 16U;
static const uint16_t ws2812Bit1LowDelayCycle = 10U;
static const uint16_t ws2812ResetDelayCycle = 2000U;

/* WS2812颜色缓存 */
static ws2812Color_t ws2812ColorBuf[WS2812_LED_MAX_COUNT];
/* 当前控制引脚ID */
static uint8_t ws2812CtrlGpioId = WS2812_DEP_CTRL_GPIO_DEFAULT;
/* 当前控制引脚端口信息 */
static commonGpioPin_t ws2812CtrlPin;
/* 当前控制引脚是否有效 */
static uint8_t ws2812CtrlPinReady = 0U;

/* 将控制引脚输出为高/低电平 */
static void __DRIVER_WS2812_WriteLevel(uint8_t level){
    if(ws2812CtrlPinReady == 0U) return;

    if(level == WS2812_DEP_GPIO_LEVEL_HIGH){
        ws2812CtrlPin.port->BSRR = ws2812CtrlPin.pin;
    } else {
        ws2812CtrlPin.port->BRR = ws2812CtrlPin.pin;
    }
}

/* 输出一个逻辑0时隙 */
static void __DRIVER_WS2812_WriteBit0(void){
    __DRIVER_WS2812_WriteLevel(WS2812_DEP_GPIO_LEVEL_HIGH);
    WS2812_DEP_DELAY_CYCLE(ws2812Bit0HighDelayCycle);
    __DRIVER_WS2812_WriteLevel(WS2812_DEP_GPIO_LEVEL_LOW);
    WS2812_DEP_DELAY_CYCLE(ws2812Bit0LowDelayCycle);
}

/* 输出一个逻辑1时隙 */
static void __DRIVER_WS2812_WriteBit1(void){
    __DRIVER_WS2812_WriteLevel(WS2812_DEP_GPIO_LEVEL_HIGH);
    WS2812_DEP_DELAY_CYCLE(ws2812Bit1HighDelayCycle);
    __DRIVER_WS2812_WriteLevel(WS2812_DEP_GPIO_LEVEL_LOW);
    WS2812_DEP_DELAY_CYCLE(ws2812Bit1LowDelayCycle);
}

/* 按高位在前的顺序发送1字节 */
static void __DRIVER_WS2812_SendByte(uint8_t data){
    for(uint8_t bitMask = 0x80U; bitMask != 0U; bitMask >>= 1U){
        if((data & bitMask) != 0U){
            __DRIVER_WS2812_WriteBit1();
        } else {
            __DRIVER_WS2812_WriteBit0();
        }
    }
}

/* 发送单颗灯珠颜色数据，按GRB顺序输出 */
static void __DRIVER_WS2812_SendColor(const ws2812Color_t *color){
    __DRIVER_WS2812_SendByte(color->green);
    __DRIVER_WS2812_SendByte(color->red);
    __DRIVER_WS2812_SendByte(color->blue);
}

/* 输出复位锁存时隙 */
static void __DRIVER_WS2812_SendReset(void){
    __DRIVER_WS2812_WriteLevel(WS2812_DEP_GPIO_LEVEL_LOW);
    WS2812_DEP_DELAY_CYCLE(ws2812ResetDelayCycle);
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化WS2812驱动 */
void DRIVER_WS2812_Init(void){
    memset(ws2812ColorBuf, 0, sizeof(ws2812ColorBuf));
    memset(&ws2812CtrlPin, 0, sizeof(ws2812CtrlPin));
    ws2812CtrlGpioId = WS2812_DEP_CTRL_GPIO_DEFAULT;
    ws2812CtrlPinReady = 0U;
}

/* 设置控制引脚 */
uint8_t DRIVER_WS2812_SetCtrlGpio(uint8_t gpioId){
    commonGpioPin_t pinInfo;

    if(WS2812_DEP_GPIO_GET_PIN_INFO(gpioId, &pinInfo) == 0U){
        memset(&ws2812CtrlPin, 0, sizeof(ws2812CtrlPin));
        ws2812CtrlGpioId = WS2812_DEP_CTRL_GPIO_DEFAULT;
        ws2812CtrlPinReady = 0U;
        return 0U;
    }

    ws2812CtrlPin = pinInfo;
    ws2812CtrlGpioId = gpioId;
    ws2812CtrlPinReady = 1U;
    WS2812_DEP_GPIO_WRITE(ws2812CtrlGpioId, WS2812_DEP_GPIO_LEVEL_LOW);
    return 1U;
}

/* 设置指定灯珠颜色 */
void DRIVER_WS2812_SetColor(uint16_t ledId, uint8_t red, uint8_t green, uint8_t blue){
    if(ledId >= WS2812_LED_MAX_COUNT) return;

    ws2812ColorBuf[ledId].red = red;
    ws2812ColorBuf[ledId].green = green;
    ws2812ColorBuf[ledId].blue = blue;
}

/* 设置全部灯珠颜色 */
void DRIVER_WS2812_SetAllColor(uint8_t red, uint8_t green, uint8_t blue){
    for(uint16_t i = 0U; i < WS2812_LED_MAX_COUNT; i++){
        ws2812ColorBuf[i].red = red;
        ws2812ColorBuf[i].green = green;
        ws2812ColorBuf[i].blue = blue;
    }
}

/* 清空全部灯珠颜色缓存 */
void DRIVER_WS2812_Clear(void){
    DRIVER_WS2812_SetAllColor(0U, 0U, 0U);
}

/* 刷新级联灯珠显示 */
void DRIVER_WS2812_Refresh(uint16_t ledCount){
    uint32_t primask;

    if(ws2812CtrlPinReady == 0U) return;
    if(ledCount > WS2812_LED_MAX_COUNT){
        ledCount = WS2812_LED_MAX_COUNT;
    }

    primask = WS2812_DEP_CRITICAL_ENTER();
    __DRIVER_WS2812_SendReset();
    for(uint16_t i = 0U; i < ledCount; i++){
        __DRIVER_WS2812_SendColor(&ws2812ColorBuf[i]);
    }
    __DRIVER_WS2812_SendReset();
    WS2812_DEP_CRITICAL_EXIT(primask);
}
