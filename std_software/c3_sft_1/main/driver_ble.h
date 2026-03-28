/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/* ============================================================
 * 文件说明：
 *   BLE 驱动模块头文件
 *   提供：
 *   1. 16 字节协议帧常量定义
 *   2. bleInfo_t 模块管理结构体
 *   3. GATT 初始化与注册回调声明
 *   4. 协议发送及 GAP 状态钩子声明
 * ============================================================ */

#ifndef DRIVER_BLE_H
#define DRIVER_BLE_H

#include <stdint.h>
#include <stdbool.h>
#include "nimble/ble.h"
#include "modlog/modlog.h"
#include "esp_peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ble_hs_cfg;
struct ble_gatt_register_ctxt;

/* ============================================================
 * Alert Notification Service（ANS）标准 UUID（蓝牙 SIG 定义）
 * ============================================================ */
#define GATT_SVR_SVC_ALERT_UUID             0x1811
#define GATT_SVR_CHR_SUP_NEW_ALERT_CAT_UUID 0x2A47
#define GATT_SVR_CHR_NEW_ALERT              0x2A46
#define GATT_SVR_CHR_SUP_UNR_ALERT_CAT_UUID 0x2A48
#define GATT_SVR_CHR_UNR_ALERT_STAT_UUID    0x2A45
#define GATT_SVR_CHR_ALERT_NOT_CTRL_PT      0x2A44

/* ============================================================
 * 16 字节协议帧常量（与 STM32 UART 协议格式相同）
 * ============================================================ */
#define BLE_FRAME_LEN       16      /* 固定帧长度 */
#define BLE_DATA_LEN        10      /* 数据段最大长度 */
#define BLE_FRAME_HEADER1   0x55U   /* 包头字节 1 */
#define BLE_FRAME_HEADER2   0xAAU   /* 包头字节 2 */
#define BLE_FRAME_TAIL      0xFFU   /* 包尾字节 */

/* ============================================================
 * bleDataPayload_t — 10 字节数据段联合体
 *   raw[]    原始字节访问
 *   具名字段 直接访问协议变量（小端序）：
 *     var_4b_1  [byte 0~3]   4 字节变量 1
 *     var_4b_2  [byte 4~7]   4 字节变量 2
 *     var_1b_1  [byte 8]     1 字节变量 1
 *     var_1b_2  [byte 9]     1 字节变量 2
 * ============================================================ */
typedef union {
    uint8_t raw[BLE_DATA_LEN];
    struct __attribute__((packed)) {
        uint32_t var_4b_1;
        uint32_t var_4b_2;
        uint8_t  var_1b_1;
        uint8_t  var_1b_2;
    };
} bleDataPayload_t;

/* ============================================================
 * bleInfo_t — driver_ble 模块公有管理结构体
 * ============================================================ */
typedef struct {
    bool             connected;    /* BLE 连接状态 */
    bool             subscribed;   /* APP 是否已订阅 Chr1 通知 */
    uint16_t         conn_handle;  /* 当前连接句柄 */
    uint8_t          tx_cnt;       /* 发送帧计数器，0x00~0xFF 循环 */
    uint8_t          cmd;          /* APP 最近一帧的控制字段（ctrl byte[2]） */
    bleDataPayload_t bleRxFrame;   /* 10 字节数据段，支持具名字段访问 */
    bleDataPayload_t bleTxFrame;   /* 10 字节数据段，支持具名字段访问 */
    bool             frame_ready;  /* 新帧就绪标志；读取后应清零 */
} bleInfo_t;

extern bleInfo_t bleInfo;

/* ============================================================
 * GATT 初始化与注册回调（实现位于 driver_ble.c）
 * ============================================================ */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
int  gatt_svr_init(void);

/* ============================================================
 * 协议发送接口
 * 说明：构建 16 字节帧并通过 Chr1 Notify 推送给 APP。
 *       未连接或未订阅时返回 -1。
 * 参数：ctrl —— 控制字段；data —— 数据指针（可 NULL）；len —— 有效字节数
 * ============================================================ */
int driver_ble_send(uint8_t ctrl, const uint8_t *data, uint8_t len);

/* ============================================================
 * GAP 状态钩子（由 main.c 的 GAP 事件回调调用）
 * ============================================================ */
void driver_ble_on_connect(uint16_t conn_handle);
void driver_ble_on_disconnect(void);
void driver_ble_on_subscribe(uint16_t attr_handle, bool cur_notify);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_BLE_H */
