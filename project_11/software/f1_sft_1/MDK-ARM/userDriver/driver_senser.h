#ifndef __DRIVER_SENSER_H__
#define __DRIVER_SENSER_H__

#include <stdint.h>
#include "stdlib_common.h"

/*============================================================================
 * 向下依赖宏（依赖 stdlib_common / main.h HAL定义）
 *============================================================================*/

/* CH1(USER_IO_1)：EXTI中断触发，ISR捕获DWT时间戳 */
/* CH2(USER_IO_2)：普通GPIO输入，由上层µs级紧循环轮询读取 */
#define SENSER_DEP_CH1_GPIO_ID     GPIO_ID_USER_IO_1    /* 对管1 GPIO ID（供stdlib通用读取使用） */
#define SENSER_DEP_CH2_GPIO_ID     GPIO_ID_USER_IO_2    /* 对管2 GPIO ID（供stdlib通用读取使用） */
#define SENSER_DEP_CH1_PIN         USER_IO_1_Pin        /* 对管1 HAL引脚，用于EXTI回调匹配 */
#define SENSER_DEP_BLOCKED_LEVEL   GPIO_LEVEL_LOW       /* 有遮挡时输出低电平 */

/* CH2直接寄存器访问（用于µs级紧循环轮询，约3时钟周期/次，远快于stdlib封装调用）
 * 依赖 main.h 中 CubeMX 生成的 USER_IO_2_GPIO_Port / USER_IO_2_Pin 定义 */
#define SENSER_DEP_CH2_PORT        USER_IO_2_GPIO_Port  /* 对管2 GPIO端口寄存器 */
#define SENSER_DEP_CH2_PIN_MASK    USER_IO_2_Pin        /* 对管2 GPIO引脚掩码 */

/*============================================================================
 * 向上提供宏（driver层向func/task层提供）
 *============================================================================*/

#define SENSER_CH1           (0U)   /* 对管1通道索引 */
#define SENSER_CH2           (1U)   /* 对管2通道索引 */
#define SENSER_CH_COUNT      (2U)   /* 通道总数 */

#define SENSER_BLOCKED       (1U)   /* 有遮挡 */
#define SENSER_UNBLOCKED     (0U)   /* 无遮挡 */

/* 单通道信息结构体 */
typedef struct {
    uint8_t  isBlocked;   /* 当前是否被遮挡：1=遮挡，0=未遮挡 */
    uint32_t trigCyc;     /* 最近一次遮挡触发时的DWT周期计数（由中断写入） */
} senserChInfo_t;

/* 红外对管模块信息结构体 */
typedef struct {
    senserChInfo_t ch[SENSER_CH_COUNT];   /* 各通道状态 */
} senserInfo_t;

extern senserInfo_t senserInfo;

/* 初始化红外对管模块，清零所有通道状态 */
void DRIVER_SENSER_Init(void);

/* 读取指定通道当前是否被遮挡，返回 SENSER_BLOCKED 或 SENSER_UNBLOCKED */
uint8_t DRIVER_SENSER_IsBlocked(uint8_t chId);

/* 轮询更新指定通道状态：读取GPIO，检测遮挡上升沿并记录DWT时间戳到trigCyc，更新isBlocked
 * 适用于无中断的通道（如CH2），用于初始状态同步或低频调用 */
void DRIVER_SENSER_PollUpdate(uint8_t chId);

/* 高速阻塞等待CH2遮挡上升沿：直接读取GPIO寄存器（约3时钟周期/次），实现µs级轮询
 * 检测到UNBLOCKED→BLOCKED上升沿时记录DWT时间戳到senserInfo.ch[SENSER_CH2].trigCyc
 * startCyc：超时起始DWT计数（通常为CH1触发时刻）；timeoutMs：最长等待时间（ms）
 * 返回SENSER_BLOCKED表示成功捕获，SENSER_UNBLOCKED表示超时
 * 调用前须先调用DRIVER_SENSER_PollUpdate同步CH2初始状态，防止将已有遮挡误判为新触发
 * 建议在调用期间暂停FreeRTOS调度器（vTaskSuspendAll），防止任务切换引入轮询延迟 */
uint8_t DRIVER_SENSER_WaitCh2Blocked(uint32_t startCyc, uint32_t timeoutMs);

#endif
