#ifndef __STDLIB_DWT_H__
#define __STDLIB_DWT_H__

#include "main.h"
#include <stdint.h>

/*============================================================================
 * 内部配置
 *============================================================================*/

/* 无内部配置项，DWT 为 Cortex-M 固定硬件资源。 */

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化并启动 DWT 周期计数器（CYCCNT），重复调用无副作用。 */
void STDLIB_DWT_Init(void);

/* 获取当前 DWT 周期计数值，可用于计算两次调用之间经过的 CPU 周期数。 */
static inline uint32_t __STDLIB_DWT_GetCyc(void){
  return DWT->CYCCNT;
}

/* 从 startCyc 开始自旋等待，直到经过 cycles 个 CPU 周期。
 * 支持 CYCCNT 溢出（无符号减法自动处理）。 */
static inline void __STDLIB_DWT_WaitCyc(uint32_t startCyc, uint32_t cycles){
  while((DWT->CYCCNT - startCyc) < cycles);
}

#endif
