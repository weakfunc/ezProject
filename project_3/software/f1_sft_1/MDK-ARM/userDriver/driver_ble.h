#ifndef __DRIVER_BLE_H__
#define __DRIVER_BLE_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 *============================================================================*/
#define BLE_DEP_UART_PORT                           UART_PORT1
#define BLE_DEP_UART_SEND_FRAME(cmd, data, len)     STDLIB_USART_SendFrame(BLE_DEP_UART_PORT, (cmd), (data), (len))
#define BLE_DEP_UART_GET_FRAME(cmd, data, len)      STDLIB_USART_GetFrame(BLE_DEP_UART_PORT, (cmd), (data), (len))

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/
#define BLE_DATA_MAX_LEN                       (32U)

/* BLE数据联合体 */
typedef union {
  uint8_t bytes[BLE_DATA_MAX_LEN];
  /* USER VAR START*/

  /* USER VAR END*/
} bleDataUnion_t;

/* BLE发送信息结构体 */
typedef struct {
  uint8_t cmd;
  uint8_t dataLen;
  bleDataUnion_t data;
} bleTxInfo_t;

/* BLE接收信息结构体 */
typedef struct {
  uint8_t cmd;
  uint8_t dataLen;
  bleDataUnion_t data;
} bleRxInfo_t;

/* BLE模块信息结构体 */
typedef struct {
  bleRxInfo_t rxInfo;     /* 最近一次接收数据 */
  uint8_t     hasNewData; /* 是否有新数据标志 */
} bleInfo_t;

extern bleInfo_t bleInfo;

/* 初始化BLE驱动 */
void DRIVER_BLE_Init(void);

/* 清空BLE接收缓存 */
void DRIVER_BLE_Reset(void);

/* 发送一帧BLE数据 */
void DRIVER_BLE_Send(const bleTxInfo_t *txInfo);

/* 轮询BLE接收缓存并提取一帧新数据 */
void DRIVER_BLE_Updata(void);

/* 查询是否收到新的BLE数据 */
uint8_t DRIVER_BLE_HasNewData(void);

/* 获取最近一次收到的BLE数据 */
uint8_t DRIVER_BLE_GetRxInfo(bleRxInfo_t *rxInfo);

#endif
