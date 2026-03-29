#ifndef __DRIVER_QR_H__
#define __DRIVER_QR_H__

#include <stdint.h>
#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 *============================================================================*/
/* 二维码模块使用的串口端口 */
#define QR_DEP_UART_PORT                  UART_PORT2
/* 注册字节回调到 USART2 */
#define QR_DEP_UART_SET_CUSTOM_CB(cb)     STDLIB_USART_SetCustomCb(QR_DEP_UART_PORT, (cb))

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/

/* 二维码模块信息结构体 */
typedef struct {
  uint32_t data;       /* 最新解析数据（大端序帧第5至第8字节） */
  uint8_t  hasNewData; /* 是否有新数据标志 */
} qrInfo_t;

extern qrInfo_t qrInfo;

/* 初始化二维码扫描驱动，绑定 USART2 字节回调 */
void DRIVER_QR_Init(void);

/* 清空缓存与解析状态 */
void DRIVER_QR_Reset(void);

/* 查询是否有新的二维码数据到达 */
uint8_t DRIVER_QR_HasNewData(void);

/* 获取最新二维码数据（帧第5至第8字节，大端序存入 uint32），有新数据返回1，否则返回0 */
uint8_t DRIVER_QR_GetData(uint32_t *data);

#endif
