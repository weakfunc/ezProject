#include "stdlib_common.h"
#include "main.h"

/* HAL GPIO 映射表。 */
static const commonGpioMap_t gpioMap[GPIO_ID_COUNT] = {
    [GPIO_ID_USER_IO_1] = { USER_IO_1_GPIO_Port, USER_IO_1_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
    [GPIO_ID_USER_IO_2] = { USER_IO_2_GPIO_Port, USER_IO_2_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
    [GPIO_ID_USER_IO_3] = { USER_IO_3_GPIO_Port, USER_IO_3_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
    [GPIO_ID_USER_IO_4] = { USER_IO_4_GPIO_Port, USER_IO_4_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
    [GPIO_ID_USER_IO_5] = { USER_IO_5_GPIO_Port, USER_IO_5_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
    [GPIO_ID_USER_IO_6] = { USER_IO_6_GPIO_Port, USER_IO_6_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
    [GPIO_ID_USER_IO_7] = { USER_IO_7_GPIO_Port, USER_IO_7_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
    [GPIO_ID_USER_IO_8] = { USER_IO_8_GPIO_Port, USER_IO_8_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
    [GPIO_ID_I2C_SDA]   = { I2C_SDA_GPIO_Port, I2C_SDA_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
    [GPIO_ID_I2C_SCL]   = { I2C_SDL_GPIO_Port, I2C_SDL_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
    [GPIO_ID_KEY_2]     = { KEY_2_GPIO_Port, KEY_2_Pin, COMMON_GPIO_DIR_INPUT, GPIO_PIN_SET },
    [GPIO_ID_KEY_1]     = { KEY_1_GPIO_Port, KEY_1_Pin, COMMON_GPIO_DIR_INPUT, GPIO_PIN_SET },
    [GPIO_ID_KEY_3]     = { KEY_3_GPIO_Port, KEY_3_Pin, COMMON_GPIO_DIR_INPUT, GPIO_PIN_SET },
    [GPIO_ID_RGB_G]     = { RGB_G_GPIO_Port, RGB_G_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_RESET },
    [GPIO_ID_RGB_R]     = { RGB_R_GPIO_Port, RGB_R_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_RESET },
    [GPIO_ID_I2C_SDA_2] = { I2C_SDA_2_GPIO_Port, I2C_SDA_2_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
    [GPIO_ID_I2C_SCL_2] = { I2C_SCL_2_GPIO_Port, I2C_SCL_2_Pin, COMMON_GPIO_DIR_OUTPUT, GPIO_PIN_SET },
};

/* 检查 GPIO ID 是否映射到有效引脚。 */
static uint8_t __STDLIB_COMMON_GpioIsValidId(uint8_t gpioId){
    if(gpioId >= GPIO_ID_COUNT) return 0U;
    if(gpioMap[gpioId].port == NULL) return 0U;
    return 1U;
}

/* 初始化 stdlib 公共外设。 */
void STDLIB_COMMON_PeriphInit(void){
    STDLIB_COMMON_GpioInit();
}

/* 恢复所有已映射输出脚的默认电平。 */
void STDLIB_COMMON_GpioInit(void){
    for(uint8_t i = 0U; i < GPIO_ID_COUNT; i++){
        if(__STDLIB_COMMON_GpioIsValidId(i) == 0U) continue;
        if(gpioMap[i].dir == COMMON_GPIO_DIR_OUTPUT){
            HAL_GPIO_WritePin(gpioMap[i].port, gpioMap[i].pin, gpioMap[i].initState);
        }
    }
}

/* 向指定 GPIO 写入输出电平。 */
void STDLIB_COMMON_GpioWrite(uint8_t gpioId, uint8_t level){
    if(__STDLIB_COMMON_GpioIsValidId(gpioId) == 0U) return;
    if(gpioMap[gpioId].dir != COMMON_GPIO_DIR_OUTPUT) return;

    HAL_GPIO_WritePin(gpioMap[gpioId].port,
                      gpioMap[gpioId].pin,
                      (level == 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* 翻转指定 GPIO 的输出状态。 */
void STDLIB_COMMON_GpioToggle(uint8_t gpioId){
    if(__STDLIB_COMMON_GpioIsValidId(gpioId) == 0U) return;
    if(gpioMap[gpioId].dir != COMMON_GPIO_DIR_OUTPUT) return;

    HAL_GPIO_TogglePin(gpioMap[gpioId].port, gpioMap[gpioId].pin);
}

/* 读取指定 GPIO 当前电平。 */
uint8_t STDLIB_COMMON_GpioRead(uint8_t gpioId){
    GPIO_PinState state;

    if(__STDLIB_COMMON_GpioIsValidId(gpioId) == 0U) return GPIO_LEVEL_LOW;

    state = HAL_GPIO_ReadPin(gpioMap[gpioId].port, gpioMap[gpioId].pin);
    return (state == GPIO_PIN_SET) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

/* 获取指定 GPIO 的端口和引脚信息。 */
uint8_t STDLIB_COMMON_GpioGetPinInfo(uint8_t gpioId, commonGpioPin_t *pinInfo){
    if(pinInfo == NULL) return 0U;
    if(__STDLIB_COMMON_GpioIsValidId(gpioId) == 0U) return 0U;
    if(gpioMap[gpioId].dir != COMMON_GPIO_DIR_OUTPUT) return 0U;

    pinInfo->port = gpioMap[gpioId].port;
    pinInfo->pin = gpioMap[gpioId].pin;
    return 1U;
}

/* 返回系统毫秒节拍。 */
uint32_t STDLIB_COMMON_GetTickMs(void){
    return HAL_GetTick();
}

/* 进入临界区并保存进入前的中断状态。 */
uint32_t STDLIB_COMMON_EnterCritical(void){
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

/* 退出临界区，并恢复保存的中断状态。 */
void STDLIB_COMMON_ExitCritical(uint32_t primask){
    if(primask == 0U){
        __enable_irq();
    }
}
