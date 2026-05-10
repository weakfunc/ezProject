/*============================================================================
 * README
 *
 * 修改记录：
 * 2026-05-04：增加 HX711 称重读取、校准参数和周期缓存更新 API。
 * 原因：task 层需要获取当前重量并保存到 senserInfo_t。
 *
 *============================================================================*/

#include "driver_senser.h"

/* 称重传感器模块对外状态缓存，上层任务直接读取该结构体即可。 */
senserInfo_t senserInfo;

/* PD_SCK 是 HX711 的时钟/断电影响脚，空闲时必须保持低电平。 */
static void __DRIVER_SENSER_SckWrite(uint8_t level){
    HAL_GPIO_WritePin(SENSER_DEP_HX711_PD_SCK_GPIO_Port,
                      SENSER_DEP_HX711_PD_SCK_Pin,
                      (level == 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* DOUT 高电平表示数据未准备好，低电平表示可开始读取 24bit 数据。 */
static uint8_t __DRIVER_SENSER_DoutRead(void){
    GPIO_PinState state;

    state = HAL_GPIO_ReadPin(SENSER_DEP_HX711_DOUT_GPIO_Port,
                             SENSER_DEP_HX711_DOUT_Pin);
    return (state == GPIO_PIN_SET) ? 1U : 0U;
}

void DRIVER_SENSER_Init(void){
    /* scaleCountPerG 默认给 1，保证未标定前仍能观察 ADC 变化趋势。 */
    senserInfo.currentWeightG   = 0.0f;
    senserInfo.rawAdc           = 0U;
    senserInfo.zeroOffset       = SENSER_HX711_DEFAULT_ZERO_OFFSET;
    senserInfo.scaleCountPerG   = SENSER_HX711_DEFAULT_SCALE_COUNT_PER_G;
    senserInfo.isReady          = 0U;
    senserInfo.hasNewData       = 0U;
    senserInfo.updateCount      = 0U;
    senserInfo.notReadyCount    = 0U;
    senserInfo.lastUpdateTickMs = 0U;

    __DRIVER_SENSER_SckWrite(0U);
}

uint8_t DRIVER_SENSER_IsReady(void){
    return (__DRIVER_SENSER_DoutRead() == 0U) ? 1U : 0U;
}

uint8_t DRIVER_SENSER_ReadRaw(uint32_t *rawAdc){
    uint8_t i;
    uint32_t count = 0U;
    uint32_t primask;

    if(rawAdc == NULL) return 0U;
    if(DRIVER_SENSER_IsReady() == 0U) return 0U;

    /* 读数时序只有几十微秒，临界区可避免任务切换拉长 PD_SCK 高电平。 */
    primask = STDLIB_COMMON_EnterCritical();

    /* 前 24 个时钟读出 ADC 数据，MSB first。 */
    for(i = 0U; i < 24U; i++){
        __DRIVER_SENSER_SckWrite(1U);
        STDLIB_DWT_DelayUs(1U);
        count <<= 1U;
        __DRIVER_SENSER_SckWrite(0U);
        STDLIB_DWT_DelayUs(1U);
        if(__DRIVER_SENSER_DoutRead() != 0U){
            count++;
        }
    }

    /* 额外时钟数选择下一次转换通道/增益，当前为 A 通道 128 倍。 */
    for(i = 0U; i < SENSER_HX711_GAIN_PULSE; i++){
        __DRIVER_SENSER_SckWrite(1U);
        STDLIB_DWT_DelayUs(1U);
        __DRIVER_SENSER_SckWrite(0U);
        STDLIB_DWT_DelayUs(1U);
    }

    STDLIB_COMMON_ExitCritical(primask);

    /* 按 HX711 C 参考例程转换输出码，便于上层用无符号数做零点标定。 */
    *rawAdc = (count ^ 0x800000U);
    return 1U;
}

void DRIVER_SENSER_InfoUpdate(void){
    uint32_t rawAdc;
    int32_t weightCount;

    senserInfo.hasNewData = 0U;
    senserInfo.isReady = DRIVER_SENSER_IsReady();

    if(senserInfo.isReady == 0U){
        senserInfo.notReadyCount++;
        return;
    }

    if(DRIVER_SENSER_ReadRaw(&rawAdc) == 0U){
        return;
    }

    /* 当前重量 = 去零点后的 ADC 计数 / 每克计数；scale 需用砝码实测标定。 */
    senserInfo.rawAdc = rawAdc;
    weightCount = (int32_t)rawAdc - senserInfo.zeroOffset;
    if(senserInfo.scaleCountPerG != 0.0f){
        senserInfo.currentWeightG = (float)weightCount / senserInfo.scaleCountPerG;
    }
    senserInfo.hasNewData = 1U;
    senserInfo.updateCount++;
    senserInfo.lastUpdateTickMs = STDLIB_COMMON_GetTickMs();
}

float DRIVER_SENSER_GetCurrentWeightG(void){
    return senserInfo.currentWeightG;
}

void DRIVER_SENSER_SetCalibration(int32_t zeroOffset, float scaleCountPerG){
    senserInfo.zeroOffset = zeroOffset;
    if(scaleCountPerG != 0.0f){
        senserInfo.scaleCountPerG = scaleCountPerG;
    }
}

void DRIVER_SENSER_SetZeroOffsetToCurrent(void){
    senserInfo.zeroOffset = (int32_t)senserInfo.rawAdc;
}
