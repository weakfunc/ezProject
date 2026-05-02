#ifndef __DRIVER_QR_H__
#define __DRIVER_QR_H__

#include <stdint.h>
#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 * 依赖：stdlib_usart
 *============================================================================*/
/* 二维码模块使用的串口端口 */
#define QR_DEP_UART_PORT               UART_PORT3
/* 注册USART2字节回调 */
#define QR_DEP_UART_SET_CUSTOM_CB(cb)  STDLIB_USART_SetCustomCb(QR_DEP_UART_PORT, (cb))

/*============================================================================
 * 向上提供（driver层向task层提供）
 *============================================================================*/

/* 二维码数据帧字节长度（默认1字节，可修改） */
#define QR_DATA_LEN  1U

/* 二维码模块信息结构体 */
typedef struct {
  uint8_t  data1;       /* 最新接收的第1字节数据 */
  uint8_t  hasNewData;  /* 有新数据标志，上层读取后应清零 */
  uint32_t rxTotalCnt;  /* 累计接收帧数（每满QR_DATA_LEN字节+1） */
} qrInfo_t;

extern qrInfo_t qrInfo;

/* 初始化二维码扫描驱动，注册USART2字节回调 */
void DRIVER_QR_Init(void);

#endif
