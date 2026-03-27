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

/* DWT 模块管理结构体，记录 CPU 频率、换算系数及最近一次读取的周期计数值。 */
typedef struct {
  uint32_t cpuFreqHz; /* CPU 频率，单位 Hz                                   */
  uint32_t cycPerMs;  /* 每毫秒对应的 CPU 周期数                             */
  uint32_t cycPerUs;  /* 每微秒对应的 CPU 周期数                             */
  uint32_t curCyc;    /* 最近一次调用 STDLIB_DWT_GetCyc 时的 DWT 周期计数值  */
} dwtInfo_t;

/* DWT 模块管理结构体实例，外部可读取各字段。 */
extern dwtInfo_t dwtInfo;

/* 初始化并启动 DWT 周期计数器（CYCCNT），同时填充 dwtInfo，重复调用无副作用。 */
void STDLIB_DWT_Init(void);

/* 读取当前 DWT 周期计数值，同步写入 dwtInfo.curCyc 并返回，
 * 非阻塞延时时调用本函数获取 startCyc。 */
uint32_t STDLIB_DWT_GetCyc(void);

/* 阻塞延时，单位毫秒，基于 DWT 周期计数，精度优于 SysTick 方案。 */
void STDLIB_DWT_DelayMs(uint32_t ms);

/* 阻塞延时，单位微秒，基于 DWT 周期计数，精度优于 SysTick 方案。 */
void STDLIB_DWT_DelayUs(uint32_t us);

/* 非阻塞检测：自 startCyc 起是否已过 ms 毫秒，已过返回 1，未过返回 0。
 * startCyc 由调用方通过 STDLIB_DWT_GetCyc() 在起始时刻获取并保存。 */
uint8_t STDLIB_DWT_IsElapsedMs(uint32_t startCyc, uint32_t ms);

/* 非阻塞检测：自 startCyc 起是否已过 us 微秒，已过返回 1，未过返回 0。
 * startCyc 由调用方通过 STDLIB_DWT_GetCyc() 在起始时刻获取并保存。 */
uint8_t STDLIB_DWT_IsElapsedUs(uint32_t startCyc, uint32_t us);

/* 从 startCyc 开始自旋等待，直到经过 cycles 个 CPU 周期（内部辅助，支持溢出）。 */
static inline void __STDLIB_DWT_WaitCyc(uint32_t startCyc, uint32_t cycles){
  while((DWT->CYCCNT - startCyc) < cycles);
}

/* 直接读取 DWT 周期计数器（内部辅助，inline 无函数调用开销，供时序敏感驱动使用）。 */
static inline uint32_t __STDLIB_DWT_GetCyc(void){
  return DWT->CYCCNT;
}

#endif
