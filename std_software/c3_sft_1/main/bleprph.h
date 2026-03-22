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
 *   本工程公共头文件，提供：
 *   1. Alert Notification Service（ANS）标准 UUID 宏定义
 *   2. GATT 服务器相关函数的外部声明
 * ============================================================ */

#ifndef H_BLEPRPH_
#define H_BLEPRPH_

#include <stdbool.h>
#include "nimble/ble.h"        /* NimBLE 基础类型（ble_addr_t、UUID 等） */
#include "modlog/modlog.h"     /* 模块日志接口（MODLOG_DFLT） */
#include "esp_peripheral.h"    /* ESP-IDF 外设工具（print_addr 等） */

#ifdef __cplusplus
extern "C" {
#endif

struct ble_hs_cfg;
struct ble_gatt_register_ctxt;

/* ============================================================
 * Alert Notification Service（ANS）标准 UUID 定义（蓝牙标准 16 位 UUID）
 * 参考：Bluetooth Assigned Numbers / GATT Specifications
 * ============================================================ */
#define GATT_SVR_SVC_ALERT_UUID             0x1811  /* Alert Notification Service */
#define GATT_SVR_CHR_SUP_NEW_ALERT_CAT_UUID 0x2A47  /* Supported New Alert Category */
#define GATT_SVR_CHR_NEW_ALERT              0x2A46  /* New Alert */
#define GATT_SVR_CHR_SUP_UNR_ALERT_CAT_UUID 0x2A48 /* Supported Unread Alert Category */
#define GATT_SVR_CHR_UNR_ALERT_STAT_UUID    0x2A45  /* Unread Alert Status */
#define GATT_SVR_CHR_ALERT_NOT_CTRL_PT      0x2A44  /* Alert Notification Control Point */

/* ============================================================
 * GATT 服务器函数声明（实现位于 gatt_svr.c）
 * ============================================================ */

/* GATT 服务/特征/描述符注册回调，由 main.c 注入到 ble_hs_cfg.gatts_register_cb */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

/* GATT 服务初始化，注册所有自定义服务和特征，在 app_main() 中调用 */
int gatt_svr_init(void);

#ifdef __cplusplus
}
#endif

#endif /* H_BLEPRPH_ */
