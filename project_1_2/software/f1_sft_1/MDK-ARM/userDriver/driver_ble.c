#include "driver_ble.h"
#include <string.h>

/*============================================================================
 * 内部配置（仅driver_ble模块内部使用）
 *============================================================================*/

/* BLE最近一次接收数据缓存 */
static bleRxInfo_t bleRxInfoCache;
/* BLE接收数据更新标志 */
static uint8_t bleHasNewDataFlag = 0U;

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化BLE驱动 */
void DRIVER_BLE_Init(void){
    DRIVER_BLE_Reset();
}

/* 清空BLE接收缓存 */
void DRIVER_BLE_Reset(void){
    memset(&bleRxInfoCache, 0, sizeof(bleRxInfoCache));
    bleHasNewDataFlag = 0U;
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

    memset(&bleRxInfoCache, 0, sizeof(bleRxInfoCache));
    bleRxInfoCache.cmd = rxCmd;
    bleRxInfoCache.dataLen = rxLen;
    if(rxLen != 0U){
        memcpy(bleRxInfoCache.data.bytes, rxData, rxLen);
    }
    bleHasNewDataFlag = 1U;
}

/* 查询是否存在新的BLE接收数据 */
uint8_t DRIVER_BLE_HasNewData(void){
    return bleHasNewDataFlag;
}

/* 读取最近一次接收数据，并清除更新标志 */
uint8_t DRIVER_BLE_GetRxInfo(bleRxInfo_t *rxInfo){
    if(rxInfo == NULL) return 0U;
    if(bleHasNewDataFlag == 0U) return 0U;

    *rxInfo = bleRxInfoCache;
    bleHasNewDataFlag = 0U;
    return 1U;
}
