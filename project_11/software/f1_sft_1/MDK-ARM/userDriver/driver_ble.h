#ifndef __DRIVER_BLE_H__
#define __DRIVER_BLE_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 * 依赖：stdlib_usart
 *============================================================================*/
/* BLE 模块使用的串口端口（UART_PORT1，使用内置标准协议）。 */
#define BLE_DEP_UART_PORT                   UART_PORT1
/* 通过标准协议发送一帧，数据取自 BLE_DEP_UART_INFO.standardTxFrame。 */
#define BLE_DEP_UART_SEND_FRAME(cmd)        STDLIB_USART_SendFrame(BLE_DEP_UART_PORT, (cmd))
/* 访问 BLE 端口的对外数据接口。 */
#define BLE_DEP_UART_INFO                   (usartInfo[BLE_DEP_UART_PORT])

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/

/* 数据字段长度（byte[3]~byte[12]，共 10 字节）。 */
#define BLE_FRAME_DATA_LEN      (10U)

/* STM32 发往 ESP32 的 CMD 范围。 */
#define BLE_CMD_TX_MIN          (0x09U)
#define BLE_CMD_TX_MAX          (0x12U)

/* STM32 系统信息帧 CMD。 */
#define BLE_CMD_STM32_SYS_INFO  (0x09U)

/* ESP32 发往 STM32 的 CMD 范围。 */
#define BLE_CMD_RX_MIN          (0x13U)
#define BLE_CMD_RX_MAX          (0x16U)

/* BLE 数据帧结构体。 */
typedef struct {
  uint8_t cmd;                      /* 控制字段 */
  uint8_t data[BLE_FRAME_DATA_LEN]; /* 数据内容（UTF-8，不足补 0x00） */
} bleFrame_t;

/* BLE 模块信息结构体。 */
typedef struct {
  bleFrame_t rxFrame;    /* 最近一次接收帧 */
  uint8_t    hasNewData; /* 是否有新帧到达标志 */
} bleInfo_t;

extern bleInfo_t bleInfo;

/* 初始化 BLE 驱动（UART_PORT1 使用内置标准协议，无需注册回调）。 */
void DRIVER_BLE_Init(void);

/* 清空接收缓存与帧就绪标志。 */
void DRIVER_BLE_Reset(void);

/* 通过 UART_PORT1 向 ESP32 发送一帧 BLE 数据。 */
void DRIVER_BLE_SendFrame(uint8_t cmd, const uint8_t *data, uint8_t dataLen);

/* 查询是否有新帧到达。 */
uint8_t DRIVER_BLE_HasNewData(void);

/* 获取最近一次接收帧，并清除更新标志。 */
uint8_t DRIVER_BLE_GetRxFrame(bleFrame_t *frame);

/* 发送 STM32 系统信息帧（CMD=0x09），taskCnt 装填至 var_4b_1。 */
void DRIVER_BLE_SendStm32SysInfo(uint32_t taskCnt);

#endif
