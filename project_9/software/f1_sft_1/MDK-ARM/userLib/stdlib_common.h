#ifndef __STDLIB_COMMON_H__
#define __STDLIB_COMMON_H__

#include "main.h"
#include <stdint.h>

/* GPIO 方向定义。 */
typedef enum {
    COMMON_GPIO_DIR_INPUT = 0U,
    COMMON_GPIO_DIR_OUTPUT,
} commonGpioDir_e;

/* GPIO 映射项。 */
typedef struct {
    GPIO_TypeDef   *port;
    uint16_t        pin;
    commonGpioDir_e dir;
    GPIO_PinState   initState;
} commonGpioMap_t;

/* 通用变量操作宏。 */
#define VAR_ADD(x)                      (x++);
#define VAR_ZERO(x)                     (x = 0U);
#define TASK_SUBTASK(x)                 ()

/* GPIO 电平定义。 */
#define GPIO_LEVEL_LOW                  (0U)
#define GPIO_LEVEL_HIGH                 (1U)
#define GPIO_ID_NULL                    (0xFFU)

/* GPIO ID，和 CubeMX 用户标签一一对应。 */
#define GPIO_ID_USER_IO_7               (0U)
#define GPIO_ID_USER_IO_8               (1U)
#define GPIO_ID_USER_IO_2               (2U)
#define GPIO_ID_USER_IO_6               (3U)
#define GPIO_ID_USER_IO_5               (4U)
#define GPIO_ID_I2C_SDA                 (5U)
#define GPIO_ID_I2C_SCL                 (6U)
#define GPIO_ID_USER_IO_1               (7U)
#define GPIO_ID_USER_IO_4               (8U)
#define GPIO_ID_USER_IO_3               (9U)
#define GPIO_ID_KEY_2                   (10U)
#define GPIO_ID_KEY_1                   (11U)
#define GPIO_ID_RGB_G                   (12U)
#define GPIO_ID_RGB_R                   (13U)
#define GPIO_ID_KEY_3                   (14U)
#define GPIO_ID_COUNT                   (15U)

/* GPIO 端口和引脚信息。 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} commonGpioPin_t;

/* 执行空转延时，适合极短的软件等待。 */
static inline void __STDLIB_COMMON_DelayCycle(volatile uint32_t cycleCount){
    while(cycleCount-- != 0U){
        __NOP();
    }
}

/* 初始化 stdlib 公共外设，当前包含 GPIO 默认状态恢复。 */
void STDLIB_COMMON_PeriphInit(void);

/* 将模块内所有输出脚恢复到默认电平。 */
void STDLIB_COMMON_GpioInit(void);

/* 向指定 GPIO 输出高低电平，仅对输出脚生效。 */
void STDLIB_COMMON_GpioWrite(uint8_t gpioId, uint8_t level);

/* 翻转指定 GPIO 的输出电平，仅对输出脚生效。 */
void STDLIB_COMMON_GpioToggle(uint8_t gpioId);

/* 读取指定 GPIO 当前电平，返回 GPIO_LEVEL_LOW 或 GPIO_LEVEL_HIGH。 */
uint8_t STDLIB_COMMON_GpioRead(uint8_t gpioId);

/* 获取指定 GPIO 的端口和引脚信息，仅对已映射输出脚生效。 */
uint8_t STDLIB_COMMON_GpioGetPinInfo(uint8_t gpioId, commonGpioPin_t *pinInfo);

/* 获取系统毫秒节拍，等价于 HAL_GetTick。 */
uint32_t STDLIB_COMMON_GetTickMs(void);

/* 进入临界区并返回进入前的 PRIMASK 状态。 */
uint32_t STDLIB_COMMON_EnterCritical(void);

/* 退出临界区，并按保存的 PRIMASK 状态恢复中断。 */
void STDLIB_COMMON_ExitCritical(uint32_t primask);

#endif
