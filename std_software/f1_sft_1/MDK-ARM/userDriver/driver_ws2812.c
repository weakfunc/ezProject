#include "driver_ws2812.h"
#include <string.h>

/*============================================================================
 * 内部配置（仅driver_ws2812模块内部使用）
 *============================================================================*/

/* 72MHz主频下WS2812B各时隙目标周期数（DWT硬件计数器计时，误差≤4周期≈55ns，在±150ns容差内）
 * GPIO写寄存器后立即读取CYCCNT作为起点，彻底消除函数调用开销对时序的影响。
 * T0H=400ns→29cyc, T0L=850ns→61cyc
 * T1H=800ns→58cyc, T1L=450ns→32cyc
 * Reset: >50us→3600cyc */
static const uint32_t ws2812Bit0HighCyc = 10U;
static const uint32_t ws2812Bit0LowCyc  = 61U;
static const uint32_t ws2812Bit1HighCyc = 58U;
static const uint32_t ws2812Bit1LowCyc  = 32U;
static const uint32_t ws2812ResetCyc    = 3600U;

/* WS2812模块数据结构体，每路通道独立一个实例，以数组形式管理 */
typedef struct {
  /* 基础通道数据 */
  ws2812Color_t   colorBuf[WS2812_LED_MAX_COUNT]; /* 颜色缓存 */
  commonGpioPin_t ctrlPin;                        /* 控制引脚端口信息 */
  uint8_t         ctrlGpioId;                     /* 控制引脚ID */
  uint8_t         ctrlPinReady;                   /* 引脚是否有效 */
  /* 呼吸灯状态 */
  uint8_t         breathR;                        /* 呼吸目标颜色R */
  uint8_t         breathG;                        /* 呼吸目标颜色G */
  uint8_t         breathB;                        /* 呼吸目标颜色B */
  uint8_t         breathLedCount;                 /* 呼吸灯珠数量 */
  uint8_t         breathStep;                     /* 每帧亮度变化量 */
  uint8_t         breathBright;                   /* 当前亮度（0~255） */
  uint8_t         breathDirUp;                    /* 1=增亮，0=减暗 */
  /* 流水灯状态 */
  uint8_t         chaseR;                         /* 流水颜色R */
  uint8_t         chaseG;                         /* 流水颜色G */
  uint8_t         chaseB;                         /* 流水颜色B */
  uint8_t         chaseLedCount;                  /* 流水灯珠数量 */
  uint8_t         chaseCurIdx;                    /* 当前亮起的灯珠序号 */
} ws2812_t;

/* 所有通道数据，以数组管理 */
static ws2812_t ws2812Arr[WS2812_CH_COUNT];

/*============================================================================
 * 内部时序函数
 *============================================================================*/

/* 将指定引脚输出高/低电平（用于引脚设置，不在时序路径中）*/
static void __DRIVER_WS2812_WriteLevel(commonGpioPin_t *pin, uint8_t level){
  if(level == WS2812_DEP_GPIO_LEVEL_HIGH){
    pin->port->BSRR = pin->pin;
  } else {
    pin->port->BRR = pin->pin;
  }
}

/* 在指定引脚上输出一个逻辑0时隙，GPIO写完后立即用DWT计时 */
static void __DRIVER_WS2812_WriteBit0(commonGpioPin_t *pin){
  uint32_t t;

  pin->port->BSRR = pin->pin;
  t = WS2812_DEP_DWT_GET_CYC();
  WS2812_DEP_DWT_WAIT_CYC(t, ws2812Bit0HighCyc);
  pin->port->BRR  = pin->pin;
  t = WS2812_DEP_DWT_GET_CYC();
  WS2812_DEP_DWT_WAIT_CYC(t, ws2812Bit0LowCyc);
}

/* 在指定引脚上输出一个逻辑1时隙，GPIO写完后立即用DWT计时 */
static void __DRIVER_WS2812_WriteBit1(commonGpioPin_t *pin){
  uint32_t t;

  pin->port->BSRR = pin->pin;
  t = WS2812_DEP_DWT_GET_CYC();
  WS2812_DEP_DWT_WAIT_CYC(t, ws2812Bit1HighCyc);
  pin->port->BRR  = pin->pin;
  t = WS2812_DEP_DWT_GET_CYC();
  WS2812_DEP_DWT_WAIT_CYC(t, ws2812Bit1LowCyc);
}

/* 按高位在前的顺序在指定引脚上发送1字节 */
static void __DRIVER_WS2812_SendByte(commonGpioPin_t *pin, uint8_t data){
  for(uint8_t bitMask = 0x80U; bitMask != 0U; bitMask >>= 1U){
    if((data & bitMask) != 0U){
      __DRIVER_WS2812_WriteBit1(pin);
    } else {
      __DRIVER_WS2812_WriteBit0(pin);
    }
  }
}

/* 在指定引脚上发送单颗灯珠颜色数据，按GRB顺序输出 */
static void __DRIVER_WS2812_SendColor(commonGpioPin_t *pin, const ws2812Color_t *color){
  __DRIVER_WS2812_SendByte(pin, color->green);
  __DRIVER_WS2812_SendByte(pin, color->red);
  __DRIVER_WS2812_SendByte(pin, color->blue);
}

/* 在指定引脚上输出复位锁存时隙（低电平保持>50us，DWT精确计时）*/
static void __DRIVER_WS2812_SendReset(commonGpioPin_t *pin){
  uint32_t t;

  pin->port->BRR = pin->pin;
  t = WS2812_DEP_DWT_GET_CYC();
  WS2812_DEP_DWT_WAIT_CYC(t, ws2812ResetCyc);
}

/* 按亮度比例缩放颜色分量（brightness: 0~255） */
static uint8_t __DRIVER_WS2812_ScaleColor(uint8_t colorVal, uint8_t brightness){
  return (uint8_t)((uint16_t)colorVal * brightness / 255U);
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化WS2812驱动，清空所有通道的颜色缓存和引脚状态 */
void DRIVER_WS2812_Init(void){
  memset(ws2812Arr, 0, sizeof(ws2812Arr));
  for(uint8_t i = 0U; i < WS2812_CH_COUNT; i++){
    ws2812Arr[i].ctrlGpioId  = WS2812_DEP_CTRL_GPIO_DEFAULT;
    ws2812Arr[i].breathDirUp = 1U;
  }
}

/* 设置指定通道的控制引脚，成功返回1，失败返回0 */
uint8_t DRIVER_WS2812_SetCtrlGpio(uint8_t chId, uint8_t gpioId){
  commonGpioPin_t pinInfo;

  if(chId >= WS2812_CH_COUNT) return 0U;

  if(WS2812_DEP_GPIO_GET_PIN_INFO(gpioId, &pinInfo) == 0U){
    memset(&ws2812Arr[chId].ctrlPin, 0, sizeof(ws2812Arr[chId].ctrlPin));
    ws2812Arr[chId].ctrlGpioId   = WS2812_DEP_CTRL_GPIO_DEFAULT;
    ws2812Arr[chId].ctrlPinReady = 0U;
    return 0U;
  }

  ws2812Arr[chId].ctrlPin      = pinInfo;
  ws2812Arr[chId].ctrlGpioId   = gpioId;
  ws2812Arr[chId].ctrlPinReady = 1U;
  __DRIVER_WS2812_WriteLevel(&ws2812Arr[chId].ctrlPin, WS2812_DEP_GPIO_LEVEL_LOW);
  return 1U;
}

/* 设置指定通道的单颗灯珠RGB颜色 */
void DRIVER_WS2812_SetColor(uint8_t chId, uint16_t ledId, uint8_t red, uint8_t green, uint8_t blue){
  if(chId >= WS2812_CH_COUNT) return;
  if(ledId >= WS2812_LED_MAX_COUNT) return;

  ws2812Arr[chId].colorBuf[ledId].red   = red;
  ws2812Arr[chId].colorBuf[ledId].green = green;
  ws2812Arr[chId].colorBuf[ledId].blue  = blue;
}

/* 设置指定通道全部灯珠为同一RGB颜色 */
void DRIVER_WS2812_SetAllColor(uint8_t chId, uint8_t red, uint8_t green, uint8_t blue){
  if(chId >= WS2812_CH_COUNT) return;

  for(uint16_t i = 0U; i < WS2812_LED_MAX_COUNT; i++){
    ws2812Arr[chId].colorBuf[i].red   = red;
    ws2812Arr[chId].colorBuf[i].green = green;
    ws2812Arr[chId].colorBuf[i].blue  = blue;
  }
}

/* 清空指定通道全部灯珠颜色缓存 */
void DRIVER_WS2812_Clear(uint8_t chId){
  DRIVER_WS2812_SetAllColor(chId, 0U, 0U, 0U);
}

/* 刷新指定通道前ledCount个级联灯珠 */
void DRIVER_WS2812_Refresh(uint8_t chId, uint16_t ledCount){
  uint32_t primask;

  if(chId >= WS2812_CH_COUNT) return;
  if(ws2812Arr[chId].ctrlPinReady == 0U) return;
  if(ledCount > WS2812_LED_MAX_COUNT){
    ledCount = WS2812_LED_MAX_COUNT;
  }

  primask = WS2812_DEP_CRITICAL_ENTER();
  __DRIVER_WS2812_SendReset(&ws2812Arr[chId].ctrlPin);
  for(uint16_t i = 0U; i < ledCount; i++){
    __DRIVER_WS2812_SendColor(&ws2812Arr[chId].ctrlPin, &ws2812Arr[chId].colorBuf[i]);
  }
  __DRIVER_WS2812_SendReset(&ws2812Arr[chId].ctrlPin);
  WS2812_DEP_CRITICAL_EXIT(primask);
}

/* 配置指定通道的呼吸灯效果 */
void DRIVER_WS2812_BreathSetup(uint8_t chId,
                                uint8_t red, uint8_t green, uint8_t blue,
                                uint8_t ledCount, uint8_t stepSize){
  if(chId >= WS2812_CH_COUNT) return;

  ws2812Arr[chId].breathR        = red;
  ws2812Arr[chId].breathG        = green;
  ws2812Arr[chId].breathB        = blue;
  ws2812Arr[chId].breathLedCount = ledCount;
  ws2812Arr[chId].breathStep     = (stepSize == 0U) ? 1U : stepSize;
  ws2812Arr[chId].breathBright   = 0U;
  ws2812Arr[chId].breathDirUp    = 1U;
}

/* 推进指定通道呼吸灯一帧，需以固定时间间隔周期性调用 */
void DRIVER_WS2812_BreathUpdate(uint8_t chId){
  uint8_t r;
  uint8_t g;
  uint8_t b;
  ws2812_t *ch;

  if(chId >= WS2812_CH_COUNT) return;
  ch = &ws2812Arr[chId];

  /* 更新亮度，到达两端时反向 */
  if(ch->breathDirUp == 1U){
    if((uint16_t)ch->breathBright + ch->breathStep >= 255U){
      ch->breathBright = 255U;
      ch->breathDirUp  = 0U;
    } else {
      ch->breathBright += ch->breathStep;
    }
  } else {
    if(ch->breathBright <= ch->breathStep){
      ch->breathBright = 0U;
      ch->breathDirUp  = 1U;
    } else {
      ch->breathBright -= ch->breathStep;
    }
  }

  /* 按亮度缩放颜色并刷新 */
  r = __DRIVER_WS2812_ScaleColor(ch->breathR, ch->breathBright);
  g = __DRIVER_WS2812_ScaleColor(ch->breathG, ch->breathBright);
  b = __DRIVER_WS2812_ScaleColor(ch->breathB, ch->breathBright);

  for(uint16_t i = 0U; i < ch->breathLedCount; i++){
    ch->colorBuf[i].red   = r;
    ch->colorBuf[i].green = g;
    ch->colorBuf[i].blue  = b;
  }
  DRIVER_WS2812_Refresh(chId, ch->breathLedCount);
}

/* 配置指定通道的流水灯效果 */
void DRIVER_WS2812_ChaseSetup(uint8_t chId,
                               uint8_t red, uint8_t green, uint8_t blue,
                               uint8_t ledCount){
  if(chId >= WS2812_CH_COUNT) return;

  ws2812Arr[chId].chaseR        = red;
  ws2812Arr[chId].chaseG        = green;
  ws2812Arr[chId].chaseB        = blue;
  ws2812Arr[chId].chaseLedCount = ledCount;
  ws2812Arr[chId].chaseCurIdx   = 0U;
}

/* 推进指定通道流水灯一步：熄灭全部，点亮当前序号，序号循环递进 */
void DRIVER_WS2812_ChaseUpdate(uint8_t chId){
  ws2812_t *ch;

  if(chId >= WS2812_CH_COUNT) return;
  ch = &ws2812Arr[chId];

  for(uint16_t i = 0U; i < ch->chaseLedCount; i++){
    ch->colorBuf[i].red   = 0U;
    ch->colorBuf[i].green = 0U;
    ch->colorBuf[i].blue  = 0U;
  }
  ch->colorBuf[ch->chaseCurIdx].red   = ch->chaseR;
  ch->colorBuf[ch->chaseCurIdx].green = ch->chaseG;
  ch->colorBuf[ch->chaseCurIdx].blue  = ch->chaseB;
  DRIVER_WS2812_Refresh(chId, ch->chaseLedCount);

  ch->chaseCurIdx++;
  if(ch->chaseCurIdx >= ch->chaseLedCount){
    ch->chaseCurIdx = 0U;
  }
}
