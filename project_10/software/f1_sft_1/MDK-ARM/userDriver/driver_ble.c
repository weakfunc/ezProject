/*============================================================================
 * README
 *
 *============================================================================*/

#include "driver_ble.h"
#include <string.h>

/*============================================================================
 * 私有变量
 *============================================================================*/

/* BLE 模块信息（公有，供上层直接访问）。 */
bleInfo_t bleInfo;

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化 BLE 驱动。UART_PORT1 已配置为内置标准协议，无需注册字节回调。 */
void DRIVER_BLE_Init(void){
  DRIVER_BLE_Reset();
}

/* 清空接收缓存与帧就绪标志。 */
void DRIVER_BLE_Reset(void){
  memset(&bleInfo, 0, sizeof(bleInfo));
  BLE_DEP_UART_INFO.frame_ready = false;
}

/* 打包并通过 UART_PORT1 向 ESP32 发送一帧 BLE 数据。
 * cmd    : 控制字段（应在 BLE_CMD_TX_MIN~BLE_CMD_TX_MAX 范围内）
 * data   : 数据字节流，NULL 时数据段全填 0x00
 * dataLen: 有效数据字节数（超出 BLE_FRAME_DATA_LEN 截断，不足补 0x00）
 */
void DRIVER_BLE_SendFrame(uint8_t cmd, const uint8_t *data, uint8_t dataLen){
  uint8_t copyLen;

  memset(BLE_DEP_UART_INFO.standardTxFrame.raw, 0x00U, STM32_DATA_LEN);
  if(data != NULL){
    copyLen = (dataLen > BLE_FRAME_DATA_LEN) ? BLE_FRAME_DATA_LEN : dataLen;
    memcpy(BLE_DEP_UART_INFO.standardTxFrame.raw, data, copyLen);
  }
  BLE_DEP_UART_SEND_FRAME(cmd);
}

/* 查询是否有新帧到达。 */
uint8_t DRIVER_BLE_HasNewData(void){
  return (BLE_DEP_UART_INFO.frame_ready) ? 1U : 0U;
}

/* 发送 STM32 系统信息帧（CMD=0x09）。
 * taskCnt：系统任务计数，装填至 standardTxFrame.var_4b_1，其余字段填 0x00。
 */
void DRIVER_BLE_SendStm32SysInfo(uint32_t taskCnt){
  memset(BLE_DEP_UART_INFO.standardTxFrame.raw, 0x00U, STM32_DATA_LEN);
  BLE_DEP_UART_INFO.standardTxFrame.var_4b_1 = taskCnt;
  BLE_DEP_UART_SEND_FRAME(BLE_CMD_STM32_SYS_INFO);
}

/* 读取最近一次接收帧，并清除更新标志。 */
uint8_t DRIVER_BLE_GetRxFrame(bleFrame_t *frame){
  if(frame == NULL) return 0U;
  if(!BLE_DEP_UART_INFO.frame_ready) return 0U;

  frame->cmd = BLE_DEP_UART_INFO.cmd;
  memcpy(frame->data, BLE_DEP_UART_INFO.standardRxFrame.raw, BLE_FRAME_DATA_LEN);

  bleInfo.rxFrame    = *frame;
  bleInfo.hasNewData = 0U;
  BLE_DEP_UART_INFO.frame_ready = false;

  return 1U;
}
