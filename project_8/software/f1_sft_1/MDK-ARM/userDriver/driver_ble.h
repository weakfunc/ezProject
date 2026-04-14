/* 框架兼容存根 — driver_ble.h
 * 本项目无BLE通信需求，此文件仅为满足 task_system.c / func_appcom.c 编译依赖而提供空实现。
 * 所有接口均为空操作，不实际使用BLE硬件。
 */
#ifndef __DRIVER_BLE_H__
#define __DRIVER_BLE_H__

#include <stdint.h>
#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖（driver层向stdlib层索要）
 * 依赖：stdlib_usart — UART_PORT1、STM32_DATA_LEN
 *============================================================================*/

/*============================================================================
 * 向上提供（公有结构体、API）
 *============================================================================*/

/* BLE接收帧结构体（框架协议帧格式） */
typedef struct {
  uint8_t cmd;                    /* 帧控制字节 */
  uint8_t data[STM32_DATA_LEN];   /* 数据段（10字节） */
} bleFrame_t;

/* 初始化BLE驱动（存根，本项目无BLE，不执行任何操作） */
void DRIVER_BLE_Init(void);

/* 发送一帧数据到BLE（存根，本项目无BLE，不执行任何操作） */
void DRIVER_BLE_SendFrame(uint8_t cmd, uint8_t *data, uint8_t len);

/* 获取BLE接收帧（存根，本项目无BLE，始终返回0表示无新帧） */
uint8_t DRIVER_BLE_GetRxFrame(bleFrame_t *frame);

#endif
