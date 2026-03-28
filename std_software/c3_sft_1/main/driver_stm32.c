/* ============================================================
 * 文件说明：
 *   driver_stm32 模块实现
 *   提供 ESP32-C3 与 STM32 之间的 UART 串口通信功能，
 *   封装固定 16 字节帧协议的收发逻辑。
 * ============================================================ */

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver_stm32.h"

/* ---- 模块日志标签 ---- */
static const char *TAG = "driver_stm32";

/* ---- UART 事件队列及接收缓冲区大小 ---- */
#define STM32_UART_BUF_SIZE   256
#define STM32_UART_QUEUE_SIZE 8

/* ---- 模块公有实例（约束：每模块一个公有结构体） ---- */
stm32Info_t stm32Info = {
    .uart_port   = STM32_UART_PORT,
    .baud_rate   = STM32_UART_BAUD,
    .rx_pin      = STM32_UART_RX_PIN,
    .tx_pin      = STM32_UART_TX_PIN,
    .tx_cnt        = 0,
    .cmd           = 0,
    .stm32RxFrame  = {.raw = {0}},
    .stm32TxFrame  = {.raw = {0}},
    .frame_ready   = false,
    .stm32CmdFrameArr = {
        /* [0..3] STM32→ESP32 (RX): CMD 0x09~0x0C */
        [0] = { .cmd = 0x09, .payload = {.raw = {0}} },
        [1] = { .cmd = 0x0A, .payload = {.raw = {0}} },
        [2] = { .cmd = 0x0B, .payload = {.raw = {0}} },
        [3] = { .cmd = 0x0C, .payload = {.raw = {0}} },
        /* [4..7] ESP32→STM32 (TX): CMD 0x13~0x16 */
        [4] = { .cmd = 0x13, .payload = {.raw = {0}} },
        [5] = { .cmd = 0x14, .payload = {.raw = {0}} },
        [6] = { .cmd = 0x15, .payload = {.raw = {0}} },
        [7] = { .cmd = 0x16, .payload = {.raw = {0}} },
    },
};

/* ---- UART 事件队列句柄 ---- */
static QueueHandle_t s_uart_queue;

/* ============================================================
 * 函数：crc8_calc（内部）
 * 说明：对给定数据计算 CRC8（多项式 0x07，初始值 0x00）
 * 参数：data —— 数据指针；len —— 字节数
 * 返回：CRC8 校验值
 * ============================================================ */
static uint8_t crc8_calc(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x07);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ============================================================
 * 函数：frame_parse（内部）
 * 说明：校验并解析一帧 16 字节数据，结果写入 stm32Info。
 * 参数：buf —— 指向 STM32_FRAME_LEN 字节的完整帧
 * 返回：true 表示帧合法；false 表示帧非法
 * ============================================================ */
static bool frame_parse(const uint8_t *buf)
{
    /* 1. 包头 / 包尾校验 */
    if (buf[0] != STM32_HEADER1 || buf[1] != STM32_HEADER2 ||
        buf[STM32_FRAME_LEN - 1] != STM32_TAIL) {
        return false;
    }

    /* 2. 控制字段合法性校验（仅接受 STM32→ESP32 方向） */
    uint8_t ctrl = buf[2];
    if (ctrl < STM32_CTRL_RX_MIN || ctrl > STM32_CTRL_RX_MAX) {
        ESP_LOGW(TAG, "unexpected ctrl=0x%02X", ctrl);
        return false;
    }

    /* 3. CRC8 校验（字段为 0x00 时跳过） */
    uint8_t crc_field = buf[13];
    if (crc_field != 0x00) {
        uint8_t crc_calc = crc8_calc(buf, 13);
        if (crc_calc != crc_field) {
            ESP_LOGW(TAG, "CRC mismatch: calc=0x%02X field=0x%02X", crc_calc, crc_field);
            return false;
        }
    }

    /* 4. 写入 stm32Info */
    stm32Info.cmd = ctrl;
    memcpy(stm32Info.stm32RxFrame.raw, &buf[3], STM32_DATA_LEN);
    stm32Info.frame_ready = true;

    /* 5. 按 CMD 更新帧缓存数组 */
    for (uint8_t i = 0; i < STM32_CMD_FRAME_CNT; i++) {
        if (stm32Info.stm32CmdFrameArr[i].cmd == ctrl) {
            memcpy(stm32Info.stm32CmdFrameArr[i].payload.raw,
                   &buf[3], STM32_DATA_LEN);
            break;
        }
    }

    return true;
}

/* ============================================================
 * 函数：stm32_rx_task（内部 FreeRTOS 任务）
 * 说明：持续从 UART 读取数据，在字节流中搜索合法的 16 字节帧。
 *       采用滑动窗口策略：找到包头后等待满 16 字节再做整帧解析。
 * ============================================================ */
void driver_stm32_rx_task(void *arg)
{
    (void)arg;
    uart_event_t event;
    uint8_t      byte_buf[STM32_UART_BUF_SIZE];
    uint8_t      frame_buf[STM32_FRAME_LEN];
    uint8_t      frame_pos = 0;
    bool         in_frame  = false;

    while (1) {
        if (xQueueReceive(s_uart_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (event.type != UART_DATA) {
            /* 处理错误或缓冲区溢出事件 */
            if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
                ESP_LOGW(TAG, "UART overflow, flushing");
                uart_flush_input(stm32Info.uart_port);
                xQueueReset(s_uart_queue);
                in_frame  = false;
                frame_pos = 0;
            }
            continue;
        }

        /* 读取本次到达的字节 */
        int read_len = uart_read_bytes(stm32Info.uart_port, byte_buf,
                                       (uint32_t)event.size, pdMS_TO_TICKS(20));
        if (read_len <= 0) {
            continue;
        }

        /* 逐字节解析，寻找完整帧 */
        for (int i = 0; i < read_len; i++) {
            uint8_t b = byte_buf[i];

            if (!in_frame) {
                /* 尚未进入帧：等待 0x55 0xAA 包头 */
                if (frame_pos == 0 && b == STM32_HEADER1) {
                    frame_buf[frame_pos++] = b;
                } else if (frame_pos == 1 && b == STM32_HEADER2) {
                    frame_buf[frame_pos++] = b;
                    in_frame = true;
                } else {
                    /* 包头不匹配，重置 */
                    frame_pos = 0;
                    if (b == STM32_HEADER1) {
                        frame_buf[frame_pos++] = b;
                    }
                }
            } else {
                /* 在帧内：继续收集字节直到满 16 字节 */
                frame_buf[frame_pos++] = b;
                if (frame_pos == STM32_FRAME_LEN) {
                    /* 尝试解析完整帧 */
                    if (!frame_parse(frame_buf)) {
                        ESP_LOGW(TAG, "invalid frame dropped");
                    }
                    /* 重置状态，准备接收下一帧 */
                    in_frame  = false;
                    frame_pos = 0;
                }
            }
        }
    }
}

/* ============================================================
 * 函数：driver_stm32_init
 * 说明：初始化 UART1，安装驱动，并启动接收任务。
 * ============================================================ */
void driver_stm32_init(void)
{
    /* ---- 1. 配置 UART 参数 ---- */
    uart_config_t uart_cfg = {
        .baud_rate           = (int)stm32Info.baud_rate,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(stm32Info.uart_port, &uart_cfg));

    /* ---- 2. 设置引脚 ---- */
    ESP_ERROR_CHECK(uart_set_pin(stm32Info.uart_port,
                                 stm32Info.tx_pin,
                                 stm32Info.rx_pin,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    /* ---- 3. 安装 UART 驱动（带事件队列） ---- */
    ESP_ERROR_CHECK(uart_driver_install(stm32Info.uart_port,
                                        STM32_UART_BUF_SIZE * 2,
                                        STM32_UART_BUF_SIZE * 2,
                                        STM32_UART_QUEUE_SIZE,
                                        &s_uart_queue,
                                        0));

    ESP_LOGI(TAG, "init ok, UART%d baud=%" PRIu32 " RX=%d TX=%d",
             (int)stm32Info.uart_port,
             stm32Info.baud_rate,
             stm32Info.rx_pin,
             stm32Info.tx_pin);
}

/* ============================================================
 * 函数：driver_stm32_send
 * 说明：构建 16 字节帧并通过 UART 发送给 STM32。
 * 参数：ctrl —— 控制字段（建议 STM32_CTRL_TX_MIN ~ STM32_CTRL_TX_MAX）
 *       data —— 数据内容指针（可为 NULL）
 *       len  —— 有效数据字节数（超过 STM32_DATA_LEN 的部分被截断）
 * 返回：实际发送字节数（16 表示成功），负值表示出错
 * ============================================================ */
int driver_stm32_send(uint8_t ctrl, const uint8_t *data, uint8_t len)
{
    uint8_t frame[STM32_FRAME_LEN];

    /* ---- 构建帧 ---- */
    frame[0] = STM32_HEADER1;
    frame[1] = STM32_HEADER2;
    frame[2] = ctrl;

    /* 数据段，不足补 0x00 */
    memset(&frame[3], 0x00, STM32_DATA_LEN);
    if (data != NULL && len > 0) {
        uint8_t copy_len = (len > STM32_DATA_LEN) ? STM32_DATA_LEN : len;
        memcpy(&frame[3], data, copy_len);
    }

    /* CRC8 对 [0]~[12] 共 13 字节 */
    frame[13] = crc8_calc(frame, 13);

    /* 包计数 */
    frame[14] = stm32Info.tx_cnt++;

    /* 包尾 */
    frame[15] = STM32_TAIL;

    /* ---- 发送 ---- */
    int sent = uart_write_bytes(stm32Info.uart_port, frame, STM32_FRAME_LEN);
    if (sent != STM32_FRAME_LEN) {
        ESP_LOGE(TAG, "send failed: sent=%d", sent);
    }
    return sent;
}

/* ============================================================
 * 函数：driver_stm32_send_all
 * 说明：遍历 stm32CmdFrameArr 中所有 ESP32→STM32 槽（索引 4 ~ 7），
 *       依次封包并通过 UART 发送给 STM32。
 * 返回：发送总字节数；负值表示出错
 * ============================================================ */
int driver_stm32_send_all(void)
{
    int total = 0;
    for (uint8_t i = STM32_CMD_RX_CNT; i < STM32_CMD_FRAME_CNT; i++) {
        int sent = driver_stm32_send(stm32Info.stm32CmdFrameArr[i].cmd,
                                     stm32Info.stm32CmdFrameArr[i].payload.raw,
                                     STM32_DATA_LEN);
        if (sent < 0) {
            return sent;
        }
        total += sent;
    }
    return total;
}
