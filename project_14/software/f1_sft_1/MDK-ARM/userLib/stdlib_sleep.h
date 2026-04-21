#ifndef __STDLIB_SLEEP_H__
#define __STDLIB_SLEEP_H__

#include "main.h"
#include <stdint.h>

/* 休眠进入方式。 */
#define STDLIB_SLEEP_ENTRY_WFI        (PWR_SLEEPENTRY_WFI)
#define STDLIB_SLEEP_ENTRY_WFE        (PWR_SLEEPENTRY_WFE)

/* 进入 STM32 Sleep 模式，主调压器保持开启，任意已使能中断均可唤醒。 */
void STDLIB_SLEEP_EnterSleep(void);

/* 按指定进入方式进入 STM32 Sleep 模式，sleepEntry 可选 STDLIB_SLEEP_ENTRY_WFI/WFE。 */
void STDLIB_SLEEP_EnterSleepByEntry(uint8_t sleepEntry);

/* 唤醒后的外设恢复钩子，当前预留给上层按需扩展。 */
void STDLIB_SLEEP_WakeUp(void);

#endif
