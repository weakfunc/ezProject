#ifndef __DRIVER_K210_H__
#define __DRIVER_K210_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要，依赖stdlib_usart）
 *============================================================================*/
#define K210_DEP_UART_PORT                         UART_PORT2
#define K210_DEP_UART_SET_CUSTOM_CB(parseCb)       STDLIB_USART_SetCustomCb(K210_DEP_UART_PORT, (parseCb))

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/

/* K210最近一次解析得到的数据字节 */
extern volatile uint8_t k210RxData;
/* 新帧到达标志：每成功解析一帧置1，由上层读取后清零 */
extern volatile uint8_t k210RxFlag;

/* 初始化K210驱动，注册USART2字节解析回调 */
void DRIVER_K210_Init(void);

/* 重置K210驱动解析状态和数据缓存 */
void DRIVER_K210_Reset(void);

#endif
