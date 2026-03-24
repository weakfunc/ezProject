#include "stdlib_dwt.h"

/* 初始化并启动 DWT 周期计数器。 */
void STDLIB_DWT_Init(void){
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}
