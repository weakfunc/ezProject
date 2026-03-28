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
 *   BLE 驱动模块：GATT 服务定义 + 16 字节串口自定义协议封装
 *
 *   自定义服务结构：
 *   UUID 规则：EE26TTNN-EE26-EE26-EE26-EE26EE26EE26
 *             TT=00 服务；TT=01 特征；NN=编号
 *
 *   └─ Svc1: EE260001-EE26-EE26-EE26-EE26EE26EE26
 *      └─ Chr1: EE260101-EE26-EE26-EE26-EE26EE26EE26（读/写/通知）
 *
 *   协议帧格式（16 字节，与 STM32 UART 协议相同）：
 *   [0]      0x55      包头 1
 *   [1]      0xAA      包头 2
 *   [2]      ctrl      控制字段
 *   [3~12]   data      数据内容（最多 10 字节，不足补 0x00）
 *   [13]     CRC8      对 [0]~[12] 的 CRC8；为 0x00 时默认通过
 *   [14]     CNT       包计数，0x00~0xFF 循环
 *   [15]     0xFF      包尾
 * ============================================================ */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "driver_ble.h"
#include "services/ans/ble_svc_ans.h"

/* ============================================================
 * 模块公有实例
 * ============================================================ */
bleInfo_t bleInfo = {
    .connected   = false,
    .subscribed  = false,
    .conn_handle = BLE_HS_CONN_HANDLE_NONE,
    .tx_cnt      = 0,
    .cmd         = 0,
    .bleRxFrame  = {.raw = {0}},
    .bleTxFrame  = {.raw = {0}},
    .frame_ready = false,
};

/* ============================================================
 * Svc1: EE260001-EE26-EE26-EE26-EE26EE26EE26
 * ============================================================ */
static const ble_uuid128_t gatt_svc1_uuid =
    BLE_UUID128_INIT(0x26, 0xEE, 0x26, 0xEE, 0x26, 0xEE, 0x26, 0xEE,
                     0x26, 0xEE, 0x26, 0xEE, 0x01, 0x00, 0x26, 0xEE);

/* ---- Chr1: EE260101-EE26-EE26-EE26-EE26EE26EE26 ---- */
static uint8_t  gatt_chr1_val[BLE_FRAME_LEN];
static uint16_t gatt_chr1_val_len;
static uint16_t gatt_chr1_val_handle;
static const ble_uuid128_t gatt_chr1_uuid =
    BLE_UUID128_INIT(0x26, 0xEE, 0x26, 0xEE, 0x26, 0xEE, 0x26, 0xEE,
                     0x26, 0xEE, 0x26, 0xEE, 0x01, 0x01, 0x26, 0xEE);

/* 特征访问回调前向声明 */
static int gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

/* ============================================================
 * GATT 服务表
 * ============================================================ */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &gatt_svc1_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid       = &gatt_chr1_uuid.u,
                .access_cb  = gatt_svc_access,
                .flags      = BLE_GATT_CHR_F_READ
                            | BLE_GATT_CHR_F_WRITE
                            | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &gatt_chr1_val_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

/* ============================================================
 * 函数：crc8_calc（内部）
 * 说明：CRC8 校验（多项式 0x07，初始值 0x00）
 * ============================================================ */
static uint8_t crc8_calc(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07)
                               : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* ============================================================
 * 函数：frame_parse（内部）
 * 说明：校验并解析 APP 写入的 16 字节帧，结果写入 bleInfo。
 * ============================================================ */
static void frame_parse(const uint8_t *buf)
{
    /* 包头/包尾校验 */
    if (buf[0] != BLE_FRAME_HEADER1 || buf[1] != BLE_FRAME_HEADER2 ||
        buf[BLE_FRAME_LEN - 1] != BLE_FRAME_TAIL) {
        return;
    }

    /* CRC8 校验（0x00 默认通过） */
    uint8_t crc = buf[13];
    if (crc != 0x00 && crc8_calc(buf, 13) != crc) {
        return;
    }

    bleInfo.cmd = buf[2];
    memcpy(bleInfo.bleRxFrame.raw, &buf[3], BLE_DATA_LEN);
    bleInfo.frame_ready = true;
}

/* ============================================================
 * 函数：gatt_svc_access
 * 说明：Chr1 读/写访问回调。
 *       写：解析 16 字节协议帧并存入 bleInfo。
 *       读：返回 gatt_chr1_val 当前内容。
 * ============================================================ */
static int
gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle,
                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;
    int rc;

    switch (ctxt->op) {

    case BLE_GATT_ACCESS_OP_READ_CHR:
        MODLOG_DFLT(INFO, "CHR read; conn=%d handle=%d\n",
                    conn_handle, attr_handle);
        rc = os_mbuf_append(ctxt->om, gatt_chr1_val, gatt_chr1_val_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        MODLOG_DFLT(INFO, "CHR write; conn=%d handle=%d\n",
                    conn_handle, attr_handle);
        if (OS_MBUF_PKTLEN(ctxt->om) != BLE_FRAME_LEN) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        rc = ble_hs_mbuf_to_flat(ctxt->om, gatt_chr1_val,
                                  BLE_FRAME_LEN, &gatt_chr1_val_len);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        frame_parse(gatt_chr1_val);
        return 0;

    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/* ============================================================
 * 函数：gatt_svr_register_cb
 * 说明：GATT 注册回调，打印服务/特征/描述符注册信息。
 * ============================================================ */
void
gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    (void)arg;
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        MODLOG_DFLT(DEBUG, "registered service %s handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        MODLOG_DFLT(DEBUG, "registered chr %s def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;
    case BLE_GATT_REGISTER_OP_DSC:
        MODLOG_DFLT(DEBUG, "registered dsc %s handle=%d\n",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;
    default:
        assert(0);
        break;
    }
}

/* ============================================================
 * 函数：gatt_svr_init
 * 说明：初始化标准服务，统计并注册全部自定义 GATT 服务。
 * ============================================================ */
int
gatt_svr_init(void)
{
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_ans_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/* ============================================================
 * 函数：driver_ble_send
 * 说明：构建 16 字节协议帧并通过 Chr1 Notify 推送给 APP。
 *       未连接或未订阅时直接返回 -1。
 * 参数：ctrl —— 控制字段
 *       data —— 数据内容（可为 NULL）；len —— 有效字节数
 * 返回：0 成功；-1 未就绪
 * ============================================================ */
int driver_ble_send(uint8_t ctrl, const uint8_t *data, uint8_t len)
{
    if (!bleInfo.connected || !bleInfo.subscribed) {
        return -1;
    }

    uint8_t frame[BLE_FRAME_LEN];
    frame[0] = BLE_FRAME_HEADER1;
    frame[1] = BLE_FRAME_HEADER2;
    frame[2] = ctrl;
    memset(&frame[3], 0x00, BLE_DATA_LEN);
    if (data != NULL && len > 0) {
        uint8_t copy_len = (len > BLE_DATA_LEN) ? BLE_DATA_LEN : len;
        memcpy(&frame[3], data, copy_len);
    }
    frame[13] = crc8_calc(frame, 13);
    frame[14] = bleInfo.tx_cnt++;
    frame[15] = BLE_FRAME_TAIL;

    memcpy(gatt_chr1_val, frame, BLE_FRAME_LEN);
    gatt_chr1_val_len = BLE_FRAME_LEN;
    ble_gatts_chr_updated(gatt_chr1_val_handle);

    return 0;
}

/* ============================================================
 * GAP 状态钩子：由 main.c 的 GAP 事件回调驱动
 * ============================================================ */

void driver_ble_on_connect(uint16_t conn_handle)
{
    bleInfo.connected   = true;
    bleInfo.subscribed  = false;
    bleInfo.conn_handle = conn_handle;
}

void driver_ble_on_disconnect(void)
{
    bleInfo.connected   = false;
    bleInfo.subscribed  = false;
    bleInfo.conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

void driver_ble_on_subscribe(uint16_t attr_handle, bool cur_notify)
{
    if (attr_handle == gatt_chr1_val_handle) {
        bleInfo.subscribed = cur_notify;
    }
}
