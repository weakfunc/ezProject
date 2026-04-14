#include "stdlib_dwt.h"

/* DWT 模块管理结构体实例。 */
dwtInfo_t dwtInfo;

/* 初始化并启动 DWT 周期计数器，并按 SystemCoreClock 填充 dwtInfo 换算系数。 */
void STDLIB_DWT_Init(void){
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

  dwtInfo.cpuFreqHz = SystemCoreClock;
  dwtInfo.cycPerMs  = SystemCoreClock / 1000U;
  dwtInfo.cycPerUs  = SystemCoreClock / 1000000U;
  dwtInfo.curCyc    = 0U;
}

/* 读取当前 DWT 周期计数值，同步写入 dwtInfo.curCyc 并返回。 */
uint32_t STDLIB_DWT_GetCyc(void){
  dwtInfo.curCyc = DWT->CYCCNT;
  return dwtInfo.curCyc;
}

/* 阻塞延时，单位毫秒。 */
void STDLIB_DWT_DelayMs(uint32_t ms){
  uint32_t start = STDLIB_DWT_GetCyc();
  __STDLIB_DWT_WaitCyc(start, ms * dwtInfo.cycPerMs);
}

/* 阻塞延时，单位微秒。 */
void STDLIB_DWT_DelayUs(uint32_t us){
  uint32_t start = STDLIB_DWT_GetCyc();
  __STDLIB_DWT_WaitCyc(start, us * dwtInfo.cycPerUs);
}

/* 非阻塞检测：自 startCyc 起是否已过 ms 毫秒，已过返回 1，未过返回 0。 */
uint8_t STDLIB_DWT_IsElapsedMs(uint32_t startCyc, uint32_t ms){
  return ((DWT->CYCCNT - startCyc) >= (ms * dwtInfo.cycPerMs)) ? 1U : 0U;
}

/* 非阻塞检测：自 startCyc 起是否已过 us 微秒，已过返回 1，未过返回 0。 */
uint8_t STDLIB_DWT_IsElapsedUs(uint32_t startCyc, uint32_t us){
  return ((DWT->CYCCNT - startCyc) >= (us * dwtInfo.cycPerUs)) ? 1U : 0U;
}
