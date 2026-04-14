/* 框架兼容存根 — driver_ble.c
 * 本项目无BLE通信需求，此文件仅为满足 task_system.c / func_appcom.c 编译依赖而提供空实现。
 */
#include "driver_ble.h"

/**
 * @brief  初始化BLE驱动（存根，本项目无BLE）
 */
void DRIVER_BLE_Init(void) {
  /* 本项目无BLE通信，不执行任何操作 */
}

/**
 * @brief  发送BLE数据帧（存根，本项目无BLE）
 * @param  cmd   帧控制字节（未使用）
 * @param  data  数据指针（未使用）
 * @param  len   数据长度（未使用）
 */
void DRIVER_BLE_SendFrame(uint8_t cmd, uint8_t *data, uint8_t len) {
  /* 消除未使用参数警告 */
  (void)cmd;
  (void)data;
  (void)len;
}

/**
 * @brief  获取BLE接收帧（存根，本项目无BLE，始终返回0）
 * @param  frame  接收帧指针（未使用）
 * @retval 0  始终返回无新帧
 */
uint8_t DRIVER_BLE_GetRxFrame(bleFrame_t *frame) {
  (void)frame;
  return 0U;
}
