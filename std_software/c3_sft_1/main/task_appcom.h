/* ============================================================
 * 文件说明：
 *   task_appcom 模块头文件
 *   负责将 STM32→ESP32 的数据包映射至 ESP32→APP 的 BLE 发送槽，
 *   并周期性调用 driver_ble_send_all() 推送给上位机 APP。
 *
 *   映射规则（索引一一对应）：
 *     stm32Info.stm32CmdFrameArr[i].payload
 *       → bleInfo.bleCmdFrameArr[i].payload   (i = 0 ~ BLE_CMD_TX_CNT-1)
 *
 *   举例：
 *     stm32CmdFrameArr[0] (CMD 0x09) .var_4b_1  →  bleCmdFrameArr[0] (CMD 0x17) .var_4b_1
 * ============================================================ */

#ifndef TASK_APPCOM_H
#define TASK_APPCOM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * appcomInfo_t — task_appcom 模块公有管理结构体
 * ============================================================ */
typedef struct {
    uint32_t cycle_ms;   /* 映射 & 发送周期（毫秒） */
    bool     active;     /* 任务运行标志 */
} appcomInfo_t;

extern appcomInfo_t appcomInfo;

/* ============================================================
 * 接口函数声明
 * ============================================================ */

/**
 * @brief appcom FreeRTOS 任务入口（供 main.c 通过 xTaskCreate 启动）
 *
 * 每 appcomInfo.cycle_ms 毫秒执行一次映射并调用 driver_ble_send_all()。
 * 建议栈大小：2048 字节；优先级：4
 */
void appcom_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* TASK_APPCOM_H */
