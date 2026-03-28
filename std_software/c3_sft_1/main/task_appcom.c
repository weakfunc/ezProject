/* ============================================================
 * 文件说明：
 *   task_appcom 模块实现
 *   将 STM32→ESP32 的数据包按索引映射至 ESP32→APP 的 BLE 发送槽，
 *   并周期性通过 driver_ble_send_all() 推送给上位机 APP。
 * ============================================================ */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver_stm32.h"
#include "driver_ble.h"
#include "task_appcom.h"
#include "main.h"

#define APPCOM_TAG "appcom"

/* ---- 模块公有实例 ---- */
appcomInfo_t appcomInfo = {
    .cycle_ms = 5,
    .active   = false,
};

static uint8_t s_ble_tx_poll_idx;
static uint8_t s_stm32_tx_poll_idx;

/* ============================================================
 * 函数：appcom_dbg_print_rx
 * 说明：遍历打印 APP→ESP32 的所有接收帧（CMD 0x21~0x24）
 * ============================================================ */
void appcom_dbg_print_rx(void)
{
    for (uint8_t i = 0; i < (BLE_CMD_FRAME_CNT - BLE_CMD_TX_CNT); i++) {
        uint8_t idx = BLE_CMD_TX_CNT + i;
        ESP_LOGI(APPCOM_TAG,
                 "RX[%u] cmd=0x%02X  var_4b_1=%lu  var_4b_2=%lu  var_1b_1=0x%02X  var_1b_2=0x%02X",
                 i,
                 bleInfo.bleCmdFrameArr[idx].cmd,
                 (unsigned long)bleInfo.bleCmdFrameArr[idx].payload.var_4b_1,
                 (unsigned long)bleInfo.bleCmdFrameArr[idx].payload.var_4b_2,
                 bleInfo.bleCmdFrameArr[idx].payload.var_1b_1,
                 bleInfo.bleCmdFrameArr[idx].payload.var_1b_2);
    }
}

/* 每次调用只向 STM32 发送一帧（轮询 CMD 0x13~0x16），
 * 避免背靠背连发导致 STM32 单帧缓冲溢出。 */
static void appcom_stm32_send_next_frame(void)
{
    uint8_t tx_idx = STM32_CMD_RX_CNT + s_stm32_tx_poll_idx; /* 帧槽偏移：[4..7] */

    driver_stm32_send(stm32Info.stm32CmdFrameArr[tx_idx].cmd,
                      stm32Info.stm32CmdFrameArr[tx_idx].payload.raw,
                      STM32_DATA_LEN);

    s_stm32_tx_poll_idx = (uint8_t)((s_stm32_tx_poll_idx + 1U) % STM32_CMD_TX_CNT);
}

static void appcom_ble_send_next_frame(void)
{
    uint8_t tx_idx = s_ble_tx_poll_idx;

    if (tx_idx >= BLE_CMD_TX_CNT) {
        tx_idx = 0;
        s_ble_tx_poll_idx = 0;
    }

    if (driver_ble_send(bleInfo.bleCmdFrameArr[tx_idx].cmd,
                        bleInfo.bleCmdFrameArr[tx_idx].payload.raw,
                        BLE_DATA_LEN) == 0) {
        s_ble_tx_poll_idx = (uint8_t)((tx_idx + 1U) % BLE_CMD_TX_CNT);
    }
}



/* ============================================================
 * 函数：appcom_task
 * 说明：FreeRTOS 任务主体。
 *       每 appcomInfo.cycle_ms ms 执行一次：
 *       1. 将 stm32CmdFrameArr[i].payload 复制到 bleCmdFrameArr[i].payload
 *       2. 调用 driver_ble_send_all() 遍历发送所有 ESP32→APP 帧
 * ============================================================ */
void appcom_task(void *arg)
{
    (void)arg;
    appcomInfo.active = true;
    s_ble_tx_poll_idx   = 0;
    s_stm32_tx_poll_idx = 0;

    while (1) {
        /* ================================================================
         * ESP32→APP 数据装填（手动逐字段赋值）
         * 格式：bleInfo.bleCmdFrameArr[CMD - 0x17].payload.varXXX = <来源>;
         * ================================================================ */

         
        /* ---- CMD 0x17 ← STM32 CMD 0x09 ---- */
        bleInfo.bleCmdFrameArr[0].payload.var_4b_1 = stm32Info.stm32CmdFrameArr[0].payload.var_4b_1;
        bleInfo.bleCmdFrameArr[0].payload.var_4b_2 = systemConfig.sys_time_s;
        bleInfo.bleCmdFrameArr[0].payload.var_1b_1 = stm32Info.stm32CmdFrameArr[0].payload.var_1b_1;
        bleInfo.bleCmdFrameArr[0].payload.var_1b_2 = stm32Info.stm32CmdFrameArr[0].payload.var_1b_2;

        // /* ---- CMD 0x18 ← STM32 CMD 0x0A ---- */
        bleInfo.bleCmdFrameArr[1].payload.var_4b_1 = stm32Info.stm32CmdFrameArr[1].payload.var_4b_1;
        bleInfo.bleCmdFrameArr[1].payload.var_4b_2 = stm32Info.stm32CmdFrameArr[1].payload.var_4b_2;
        bleInfo.bleCmdFrameArr[1].payload.var_1b_1 = stm32Info.stm32CmdFrameArr[1].payload.var_1b_1;
        bleInfo.bleCmdFrameArr[1].payload.var_1b_2 = stm32Info.stm32CmdFrameArr[1].payload.var_1b_2;

        /* ---- CMD 0x19 ← STM32 CMD 0x0B ---- */
        bleInfo.bleCmdFrameArr[2].payload.var_4b_1 = stm32Info.stm32CmdFrameArr[2].payload.var_4b_1;
        bleInfo.bleCmdFrameArr[2].payload.var_4b_2 = stm32Info.stm32CmdFrameArr[2].payload.var_4b_2;
        bleInfo.bleCmdFrameArr[2].payload.var_1b_1 = stm32Info.stm32CmdFrameArr[2].payload.var_1b_1;
        bleInfo.bleCmdFrameArr[2].payload.var_1b_2 = stm32Info.stm32CmdFrameArr[2].payload.var_1b_2;

        /* ---- CMD 0x1A ← STM32 CMD 0x0C ---- */
        bleInfo.bleCmdFrameArr[3].payload.var_4b_1 = stm32Info.stm32CmdFrameArr[3].payload.var_4b_1;
        bleInfo.bleCmdFrameArr[3].payload.var_4b_2 = stm32Info.stm32CmdFrameArr[3].payload.var_4b_2;
        bleInfo.bleCmdFrameArr[3].payload.var_1b_1 = stm32Info.stm32CmdFrameArr[3].payload.var_1b_1;
        bleInfo.bleCmdFrameArr[3].payload.var_1b_2 = stm32Info.stm32CmdFrameArr[3].payload.var_1b_2;

        // for(int i=0; i<4; i++){
        //     bleInfo.bleCmdFrameArr[i].payload.var_4b_2 = systemConfig.sys_time_s;
        //     bleInfo.bleCmdFrameArr[i].payload.var_1b_1 = systemConfig.sys_time_s;
        //     bleInfo.bleCmdFrameArr[i].payload.var_1b_2 = systemConfig.sys_time_s;
        // }

        /* ---- 发送所有 ESP32→APP 帧 ---- */
        driver_ble_send_all();

        /* ================================================================
         * APP→ESP32→STM32 数据装填（手动逐字段赋值）
         * 格式：stm32Info.stm32CmdFrameArr[4 + (CMD-0x13)].payload.varXXX = <来源>;
         * ================================================================ */

        /* ---- CMD 0x13 ← APP CMD 0x21 ---- */
        stm32Info.stm32CmdFrameArr[4].payload.var_4b_1 = bleInfo.bleCmdFrameArr[4].payload.var_4b_1;
        stm32Info.stm32CmdFrameArr[4].payload.var_4b_2 = bleInfo.bleCmdFrameArr[4].payload.var_4b_2;
        stm32Info.stm32CmdFrameArr[4].payload.var_1b_1 = bleInfo.bleCmdFrameArr[4].payload.var_1b_1;
        stm32Info.stm32CmdFrameArr[4].payload.var_1b_2 = bleInfo.bleCmdFrameArr[4].payload.var_1b_2;

        /* ---- CMD 0x14 ← APP CMD 0x22 ---- */
        stm32Info.stm32CmdFrameArr[5].payload.var_4b_1 = bleInfo.bleCmdFrameArr[5].payload.var_4b_1;
        stm32Info.stm32CmdFrameArr[5].payload.var_4b_2 = bleInfo.bleCmdFrameArr[5].payload.var_4b_2;
        stm32Info.stm32CmdFrameArr[5].payload.var_1b_1 = bleInfo.bleCmdFrameArr[5].payload.var_1b_1;
        stm32Info.stm32CmdFrameArr[5].payload.var_1b_2 = bleInfo.bleCmdFrameArr[5].payload.var_1b_2;

        /* ---- CMD 0x15 ← APP CMD 0x23 ---- */
        stm32Info.stm32CmdFrameArr[6].payload.var_4b_1 = bleInfo.bleCmdFrameArr[6].payload.var_4b_1;
        stm32Info.stm32CmdFrameArr[6].payload.var_4b_2 = bleInfo.bleCmdFrameArr[6].payload.var_4b_2;
        stm32Info.stm32CmdFrameArr[6].payload.var_1b_1 = bleInfo.bleCmdFrameArr[6].payload.var_1b_1;
        stm32Info.stm32CmdFrameArr[6].payload.var_1b_2 = bleInfo.bleCmdFrameArr[6].payload.var_1b_2;

        /* ---- CMD 0x16 ← APP CMD 0x24 ---- */
        stm32Info.stm32CmdFrameArr[7].payload.var_4b_1 = bleInfo.bleCmdFrameArr[7].payload.var_4b_1;
        stm32Info.stm32CmdFrameArr[7].payload.var_4b_2 = bleInfo.bleCmdFrameArr[7].payload.var_4b_2;
        stm32Info.stm32CmdFrameArr[7].payload.var_1b_1 = bleInfo.bleCmdFrameArr[7].payload.var_1b_1;
        stm32Info.stm32CmdFrameArr[7].payload.var_1b_2 = bleInfo.bleCmdFrameArr[7].payload.var_1b_2;

        /* ---- 每次只发一帧给 STM32（轮询），避免背靠背覆盖 STM32 单帧缓冲 ---- */
        appcom_stm32_send_next_frame();


        vTaskDelay(pdMS_TO_TICKS(appcomInfo.cycle_ms));
    }
}
