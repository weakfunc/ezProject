#include "stdlib_sleep.h"

void STDLIB_SLEEP_EnterSleep(void){
  STDLIB_SLEEP_EnterSleepByEntry(STDLIB_SLEEP_ENTRY_WFI);
}

void STDLIB_SLEEP_EnterSleepByEntry(uint8_t sleepEntry){
  if((sleepEntry != STDLIB_SLEEP_ENTRY_WFI) && (sleepEntry != STDLIB_SLEEP_ENTRY_WFE)){
    sleepEntry = STDLIB_SLEEP_ENTRY_WFI;
  }

  HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, sleepEntry);
}

void STDLIB_SLEEP_WakeUp(void){
  /* 预留：如果唤醒后需要恢复外设，可在此处扩展。 */
}
