/* ============================================================
 * 文件说明：
 *   driver_stm32 模块头文件
 *   负责 ESP32-C3 与 STM32 之间的 UART 串口通信，
 *   封装固定 16 字节帧协议的收发功能。
 *
 *   帧格式（16 字节）：
 *   [0]      0x55      包头 1
 *   [1]      0xAA      包头 2
 *   [2]      ctrl      控制字段（0x09~0x12: STM32→ESP32；0x13~0x16: ESP32→STM32）
 *   [3~12]   data      数据内容（UTF-8，最多 10 字节，不足补 0x00）
 *   [13]     CRC8      对 [0]~[12] 共 13 字节做 CRC8 校验；为 0x00 时默认通过
 *   [14]     CNT       包计数，0x00~0xFF 循环递增
 *   [15]     0xFF      包尾
 * ============================================================ */

#ifndef DRIVER_STM32_H
#define DRIVER_STM32_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 协议常量 ---- */
#define STM32_FRAME_LEN      16      /* 固定帧长度（字节） */
#define STM32_DATA_LEN       10      /* 数据段最大长度（字节） */
#define STM32_HEADER1        0x55U   /* 包头字节 1 */
#define STM32_HEADER2        0xAAU   /* 包头字节 2 */
#define STM32_TAIL           0xFFU   /* 包尾字节 */
#define STM32_CTRL_RX_MIN    0x09U   /* STM32→ESP32 控制字段最小值 */
#define STM32_CTRL_RX_MAX    0x12U   /* STM32→ESP32 控制字段最大值 */
#define STM32_CTRL_TX_MIN    0x13U   /* ESP32→STM32 控制字段最小值 */
#define STM32_CTRL_TX_MAX    0x16U   /* ESP32→STM32 控制字段最大值 */

/* ---- 硬件默认配置 ---- */
#define STM32_UART_PORT      UART_NUM_1   /* 使用 UART1 */
#define STM32_UART_BAUD      115200       /* 波特率 */
#define STM32_UART_RX_PIN    20           /* ESP32-C3 RX 引脚 */
#define STM32_UART_TX_PIN    21           /* ESP32-C3 TX 引脚 */

/* ============================================================
 * stm32DataPayload_t — 10 字节数据段联合体
 *   raw[]    原始字节访问
 *   具名字段 直接访问协议变量（小端序）：
 *     var_4b_1  [byte 0~3]   4 字节变量 1
 *     var_4b_2  [byte 4~7]   4 字节变量 2
 *     var_1b_1  [byte 8]     1 字节变量 1
 *     var_1b_2  [byte 9]     1 字节变量 2
 * ============================================================ */
typedef union {
    uint8_t raw[STM32_DATA_LEN];
    struct __attribute__((packed)) {
        uint32_t var_4b_1;
        uint32_t var_4b_2;
        uint8_t  var_1b_1;
        uint8_t  var_1b_2;
    };
} stm32DataPayload_t;

/* ============================================================
 * stm32CmdFrame_t — 单个 CMD 对应的帧缓存
 *   cmd     对应的控制字段值
 *   payload 该 CMD 最近一次收到的 10 字节数据
 * ============================================================ */
typedef struct {
    uint8_t            cmd;
    stm32DataPayload_t payload;
} stm32CmdFrame_t;

#define STM32_CMD_FRAME_CNT  8U   /* CMD 帧缓存数组大小 */

/* ============================================================
 * stm32Info_t — driver_stm32 模块公有管理结构体
 * ============================================================ */
typedef struct {
    /* --- 硬件配置（初始化后只读）--- */
    uart_port_t        uart_port;    /* UART 端口号 */
    uint32_t           baud_rate;    /* 波特率 */
    int                rx_pin;       /* RX 引脚编号 */
    int                tx_pin;       /* TX 引脚编号 */

    /* --- 发送状态 --- */
    uint8_t            tx_cnt;       /* 发送帧计数器，0x00~0xFF 循环 */

    /* --- 接收状态 --- */
    uint8_t            cmd;                    /* 最近一帧的控制字段（ctrl byte[2]） */
    stm32DataPayload_t stm32RxFrame;           /* 10 字节数据段，支持具名字段访问 */
    stm32DataPayload_t stm32TxFrame;           /* 10 字节数据段，支持具名字段访问 */
    bool               frame_ready;            /* 新帧就绪标志；读取后应清零 */

    /* --- CMD 帧缓存（STM32→ESP32，CMD 0x09~0x10）--- */
    stm32CmdFrame_t    stm32CmdFrameArr[STM32_CMD_FRAME_CNT];
} stm32Info_t;

/* 模块公有实例（在 driver_stm32.c 中定义） */
extern stm32Info_t stm32Info;

/* ============================================================
 * 接口函数声明
 * ============================================================ */

/**
 * @brief 初始化 driver_stm32 模块
 *        配置 UART1，安装驱动。接收任务需由调用方（main.c）另行创建。
 */
void driver_stm32_init(void);

/**
 * @brief STM32 串口接收任务入口（供 main.c 通过 xTaskCreate 启动）
 *
 * 持续从 UART 事件队列读取数据，解析 16 字节帧，
 * 结果写入 stm32Info.cmd / rx.var_4b_1 / rx.var_4b_2 / rx.var_1b_1 / rx.var_1b_2 / frame_ready。
 *
 * 建议栈大小：2048 字节；优先级：5
 */
void driver_stm32_rx_task(void *arg);

/**
 * @brief 向 STM32 发送一帧数据
 *
 * @param ctrl  控制字段，应在 STM32_CTRL_TX_MIN ~ STM32_CTRL_TX_MAX 范围内
 * @param data  数据内容指针（最多 STM32_DATA_LEN 字节），可为 NULL
 * @param len   data 的有效字节数（超出 STM32_DATA_LEN 部分被截断）
 * @return      实际发送的字节数（16 表示成功），负值表示出错
 */
int driver_stm32_send(uint8_t ctrl, const uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_STM32_H */
