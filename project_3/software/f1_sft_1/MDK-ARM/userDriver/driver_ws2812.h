#ifndef __DRIVER_WS2812_H__
#define __DRIVER_WS2812_H__

#include <stdint.h>
#include "stdlib_common.h"
#include "stdlib_dwt.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 * 依赖：stdlib_common（GPIO、临界区），stdlib_dwt（DWT周期计时）
 *============================================================================*/
#define WS2812_DEP_CTRL_GPIO_DEFAULT                        GPIO_ID_USER_IO_1
#define WS2812_DEP_GPIO_LEVEL_LOW                           GPIO_LEVEL_LOW
#define WS2812_DEP_GPIO_LEVEL_HIGH                          GPIO_LEVEL_HIGH
#define WS2812_DEP_GPIO_GET_PIN_INFO(gpioId, pinInfo)       STDLIB_COMMON_GpioGetPinInfo((gpioId), (pinInfo))
#define WS2812_DEP_GPIO_WRITE(gpioId, level)                STDLIB_COMMON_GpioWrite((gpioId), (level))
#define WS2812_DEP_CRITICAL_ENTER()                         STDLIB_COMMON_EnterCritical()
#define WS2812_DEP_CRITICAL_EXIT(primask)                   STDLIB_COMMON_ExitCritical((primask))
#define WS2812_DEP_DWT_GET_CYC()                            __STDLIB_DWT_GetCyc()
#define WS2812_DEP_DWT_WAIT_CYC(startCyc, cycles)          __STDLIB_DWT_WaitCyc((startCyc), (cycles))

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/

/* 通道数量及通道ID */
#define WS2812_CH_COUNT                       (2U)
#define WS2812_CH_0                           (0U)
#define WS2812_CH_1                           (1U)

/* 单路最大级联灯珠数 */
#define WS2812_LED_MAX_COUNT                  (16U)

/* WS2812单灯珠颜色数据 */
typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} ws2812Color_t;

/* WS2812模块信息结构体，每路通道独立一个实例，以数组形式管理 */
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
} ws2812Info_t;

extern ws2812Info_t ws2812Info[WS2812_CH_COUNT];

/* 初始化WS2812驱动，清空所有通道 */
void DRIVER_WS2812_Init(void);

/* 设置指定通道的控制引脚，成功返回1，失败返回0
 * chId: WS2812_CH_0 或 WS2812_CH_1 */
uint8_t DRIVER_WS2812_SetCtrlGpio(uint8_t chId, uint8_t gpioId);

/* 设置指定通道的单颗灯珠RGB颜色（写入缓存，需调用Refresh生效） */
void DRIVER_WS2812_SetColor(uint8_t chId, uint16_t ledId, uint8_t red, uint8_t green, uint8_t blue);

/* 设置指定通道全部灯珠为同一RGB颜色（写入缓存，需调用Refresh生效） */
void DRIVER_WS2812_SetAllColor(uint8_t chId, uint8_t red, uint8_t green, uint8_t blue);

/* 清空指定通道全部灯珠颜色缓存（写入缓存，需调用Refresh生效） */
void DRIVER_WS2812_Clear(uint8_t chId);

/* 刷新指定通道前ledCount个级联灯珠 */
void DRIVER_WS2812_Refresh(uint8_t chId, uint16_t ledCount);

/* 配置指定通道的呼吸灯效果，stepSize越大呼吸越快（1~255）*/
void DRIVER_WS2812_BreathSetup(uint8_t chId,
                                uint8_t red, uint8_t green, uint8_t blue,
                                uint8_t ledCount, uint8_t stepSize);

/* 推进指定通道呼吸灯一帧，需以固定时间间隔调用（推荐10~20ms） */
void DRIVER_WS2812_BreathUpdate(uint8_t chId);

/* 配置指定通道的流水灯效果 */
void DRIVER_WS2812_ChaseSetup(uint8_t chId,
                               uint8_t red, uint8_t green, uint8_t blue,
                               uint8_t ledCount);

/* 推进指定通道流水灯一步，需以固定时间间隔调用（推荐50~200ms） */
void DRIVER_WS2812_ChaseUpdate(uint8_t chId);

#endif
