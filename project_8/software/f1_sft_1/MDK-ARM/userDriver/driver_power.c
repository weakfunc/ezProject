/* [新增] driver_power.c — 低功耗管理驱动，框架原无此模块，本次新增 */
#include "driver_power.h"
#include "main.h"

/*============================================================================
 * API接口实现
 *============================================================================*/

/**
 * @brief  进入STM32低功耗SLEEP模式（WFI方式）
 * @note   主调压器保持开启，仅CPU时钟停止，外设继续工作
 * @note   等待任意中断（SysTick / GPIO外部中断）后自动唤醒并返回
 * @note   在FreeRTOS任务中调用时，SysTick（1ms周期）会周期性唤醒处理器
 */
void DRIVER_POWER_EnterSleep(void) {
  HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
}

/**
 * @brief  唤醒后恢复外设
 * @note   当前外设无需在驱动层重新初始化，由task层在sleepFlag清零时完成恢复
 */
void DRIVER_POWER_WakeUp(void) {
  /* 预留：如需唤醒后执行外设重初始化，在此添加 */
}
