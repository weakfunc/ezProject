/* ============================================================
 * 文件说明：
 *   task_appcom 模块实现
 *   将 STM32→ESP32 的数据包按索引映射至 ESP32→APP 的 BLE 发送槽，
 *   并周期性通过 driver_ble_send_all() 推送给上位机 APP。
 * ============================================================ */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver_stm32.h"
#include "driver_ble.h"
#include "task_appcom.h"
#include "main.h"

/* ---- 模块公有实例 ---- */
appcomInfo_t appcomInfo = {
    .cycle_ms = 100,
    .active   = false,
};

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

        /* ---- CMD 0x18 ← STM32 CMD 0x0A ---- */
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

        /* ---- 发送所有 ESP32→APP 帧 ---- */
        driver_ble_send_all();

        vTaskDelay(pdMS_TO_TICKS(appcomInfo.cycle_ms));
    }
}
