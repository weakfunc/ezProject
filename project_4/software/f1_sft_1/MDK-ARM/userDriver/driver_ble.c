#include "driver_ble.h"
#include <string.h>

/*============================================================================
 * 内部配置（仅driver_ble模块内部使用）
 *============================================================================*/

/* BLE模块数据 */
bleInfo_t bleInfo;

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化BLE驱动 */
void DRIVER_BLE_Init(void){
  DRIVER_BLE_Reset();
}

/* 清空BLE接收缓存 */
void DRIVER_BLE_Reset(void){
  memset(&bleInfo.rxInfo, 0, sizeof(bleInfo.rxInfo));
  bleInfo.hasNewData = 0U;
}

/* 按发送结构体内容打包并发送一帧数据 */
void DRIVER_BLE_Send(const bleTxInfo_t *txInfo){
  if(txInfo == NULL) return;
  if(txInfo->dataLen > BLE_DATA_MAX_LEN) return;

  BLE_DEP_UART_SEND_FRAME(txInfo->cmd, (uint8_t *)txInfo->data.bytes, txInfo->dataLen);
}

/* 从USART1标准协议缓存中提取一帧BLE数据 */
void DRIVER_BLE_Updata(void){
  uint8_t rxCmd;
  uint8_t rxLen;
  uint8_t rxData[BLE_DATA_MAX_LEN];

  if(BLE_DEP_UART_GET_FRAME(&rxCmd, rxData, &rxLen) == 0U) return;

  memset(&bleInfo.rxInfo, 0, sizeof(bleInfo.rxInfo));
  bleInfo.rxInfo.cmd = rxCmd;
  bleInfo.rxInfo.dataLen = rxLen;
  if(rxLen != 0U){
    memcpy(bleInfo.rxInfo.data.bytes, rxData, rxLen);
  }
  bleInfo.hasNewData = 1U;
}

/* 查询是否存在新的BLE接收数据 */
uint8_t DRIVER_BLE_HasNewData(void){
  return bleInfo.hasNewData;
}

/* 读取最近一次接收数据，并清除更新标志 */
uint8_t DRIVER_BLE_GetRxInfo(bleRxInfo_t *rxInfo){
  if(rxInfo == NULL) return 0U;
  if(bleInfo.hasNewData == 0U) return 0U;

  *rxInfo = bleInfo.rxInfo;
  bleInfo.hasNewData = 0U;
  return 1U;
}
