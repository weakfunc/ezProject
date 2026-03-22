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
 *   GATT 服务器定义与访问回调
 *   负责以下功能：
 *   1. 定义自定义 GATT 服务及其特征（UUID、访问权限）
 *   2. 处理特征的读写访问回调
 *   3. 处理 GATT 注册回调（服务/特征注册日志）
 *   4. 初始化并注册所有 GATT 服务
 *
 *   服务结构（当前版本，后续将扩展为 3 个服务）：
 *   └─ Service: gatt_svr_svc_uuid（128 位自定义 UUID）
 *      └─ Characteristic: gatt_svr_chr_uuid（读 / 写 / 通知 / 指示）
 * ============================================================ */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host/ble_hs.h"           /* NimBLE 主机层（GATT/ATT 接口） */
#include "host/ble_uuid.h"         /* UUID 类型定义与操作 */
#include "services/gap/ble_svc_gap.h"   /* GAP 标准服务 */
#include "services/gatt/ble_svc_gatt.h" /* GATT 标准服务 */
#include "bleprph.h"               /* 本工程头文件（UUID 宏、函数声明） */
#include "services/ans/ble_svc_ans.h"   /* Alert Notification Service（ANS，警报通知服务） */

/* ============================================================
 * 自定义 GATT 服务 UUID（128 位）
 * 对应十六进制字符串：59462f12-9543-9999-12c8-58b459a2712d
 * ============================================================ */
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                     0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);

/* ============================================================
 * 自定义特征 UUID（128 位）
 * 对应十六进制字符串：33333333-2222-2222-1111-111100000000
 * 该特征支持：读（Read）/ 写（Write）/ 通知（Notify）/ 指示（Indicate）
 * ============================================================ */
static uint8_t gatt_svr_chr_val;          /* 特征值存储变量（1 字节） */
static uint16_t gatt_svr_chr_val_handle;  /* 特征值句柄，用于触发 Notify/Indicate */
static const ble_uuid128_t gatt_svr_chr_uuid =
    BLE_UUID128_INIT(0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x11,
                     0x22, 0x22, 0x22, 0x22, 0x33, 0x33, 0x33, 0x33);

/* 特征访问回调函数前向声明 */
static int
gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle,
                struct ble_gatt_access_ctxt *ctxt,
                void *arg);

/* ============================================================
 * GATT 服务表（静态定义，注册时传入协议栈）
 * 结构：服务数组，末尾以 {0} 结束
 * ============================================================ */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /* ---- 自定义服务：UUID = gatt_svr_svc_uuid ---- */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,     /* 主服务（Primary Service） */
        .uuid = &gatt_svr_svc_uuid.u,          /* 服务 UUID */
        .characteristics = (struct ble_gatt_chr_def[])
        { {
                /* ---- 自定义特征：UUID = gatt_svr_chr_uuid ----
                 * 上位机通过写操作向 ESP32 发送数据；
                 * ESP32 通过 Notify/Indicate 向上位机推送数据。
                 * 上位机需向 CCCD 写 0x0001 开启 Notify，或写 0x0002 开启 Indicate。 */
                .uuid = &gatt_svr_chr_uuid.u,
                .access_cb = gatt_svc_access,  /* 读写访问回调 */
#if CONFIG_EXAMPLE_ENCRYPTION
                /* 启用链路加密时，要求对端建立加密连接后才能读写 */
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE
                       | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC
                       | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
#else
                /* 不加密：对端可直接读写，无需加密前提 */
                .flags = BLE_GATT_CHR_F_READ    /* 允许读取特征值 */
                       | BLE_GATT_CHR_F_WRITE   /* 允许写入特征值（有响应） */
                       | BLE_GATT_CHR_F_NOTIFY  /* 允许订阅通知（Notification，无需应答） */
                       | BLE_GATT_CHR_F_INDICATE, /* 允许订阅指示（Indication，需要应答） */
#endif
                .val_handle = &gatt_svr_chr_val_handle, /* 保存值句柄，供 Notify 使用 */
            }, {
                0, /* 特征列表结束标志 */
            }
        },
    },

    {
        0, /* 服务列表结束标志 */
    },
};

/* ============================================================
 * 函数：gatt_svr_write（内部辅助函数）
 * 说明：将 mbuf 中的数据写入目标缓冲区，并做长度合法性检查。
 * 参数：om      —— 包含写入数据的 mbuf
 *       min_len —— 允许的最小数据长度（字节）
 *       max_len —— 允许的最大数据长度（字节）
 *       dst     —— 目标缓冲区指针
 *       len     —— 实际写入的字节数（输出）
 * 返回：0 成功；BLE_ATT_ERR_* 错误码
 * ============================================================ */
static int
gatt_svr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
               void *dst, uint16_t *len)
{
    uint16_t om_len;
    int rc;

    /* 获取 mbuf 中的数据总长度 */
    om_len = OS_MBUF_PKTLEN(om);

    /* 检查数据长度是否在合法范围内 */
    if (om_len < min_len || om_len > max_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    /* 将 mbuf 中的数据拷贝到目标缓冲区（flat buffer） */
    rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

/* ============================================================
 * 函数：gatt_svc_access
 * 说明：GATT 特征访问回调，在特征被读取或写入时由协议栈调用。
 *       ctxt->op 指明操作类型（读/写）；
 *       attr_handle 指明被访问的属性句柄。
 * 参数：conn_handle —— 触发操作的连接句柄
 *       attr_handle —— 被访问的属性句柄
 *       ctxt        —— 访问上下文（包含操作类型、数据 mbuf 等）
 *       arg         —— 用户参数（未使用）
 * 返回：0 成功；BLE_ATT_ERR_* 错误码
 * ============================================================ */
static int
gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle,
                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op) {

    case BLE_GATT_ACCESS_OP_READ_CHR:
        /* ---- 读特征值操作 ---- */
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            MODLOG_DFLT(INFO, "Characteristic read; conn_handle=%d attr_handle=%d\n",
                        conn_handle, attr_handle);
        } else {
            /* BLE_HS_CONN_HANDLE_NONE 表示协议栈内部读取，非对端发起 */
            MODLOG_DFLT(INFO, "Characteristic read by NimBLE stack; attr_handle=%d\n",
                        attr_handle);
        }
        if (attr_handle == gatt_svr_chr_val_handle) {
            /* 将当前特征值追加到响应 mbuf 中返回给对端 */
            rc = os_mbuf_append(ctxt->om,
                                &gatt_svr_chr_val,
                                sizeof(gatt_svr_chr_val));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        goto unknown;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        /* ---- 写特征值操作 ---- */
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            MODLOG_DFLT(INFO, "Characteristic write; conn_handle=%d attr_handle=%d",
                        conn_handle, attr_handle);
        } else {
            MODLOG_DFLT(INFO, "Characteristic write by NimBLE stack; attr_handle=%d",
                        attr_handle);
        }
        if (attr_handle == gatt_svr_chr_val_handle) {
            /* 将对端写入的数据保存到 gatt_svr_chr_val */
            rc = gatt_svr_write(ctxt->om,
                                sizeof(gatt_svr_chr_val),
                                sizeof(gatt_svr_chr_val),
                                &gatt_svr_chr_val, NULL);
            /* 通知所有已订阅的对端该特征值已更新（触发 Notify/Indicate） */
            ble_gatts_chr_updated(attr_handle);
            MODLOG_DFLT(INFO, "Notification/Indication scheduled for "
                        "all subscribed peers.\n");
            return rc;
        }
        goto unknown;

    default:
        goto unknown;
    }

unknown:
    /* 不应到达此处：协议栈仅会对已注册的特征/描述符调用此回调 */
    assert(0);
    return BLE_ATT_ERR_UNLIKELY;
}

/* ============================================================
 * 函数：gatt_svr_register_cb
 * 说明：GATT 注册回调，在服务/特征/描述符向协议栈注册时被调用，
 *       用于输出注册信息到调试日志。
 * 参数：ctxt —— 注册上下文（包含注册操作类型及句柄）
 *       arg  —— 用户参数（未使用）
 * ============================================================ */
void
gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN]; /* UUID 字符串缓冲区 */

    switch (ctxt->op) {

    case BLE_GATT_REGISTER_OP_SVC:
        /* 服务注册完成，打印服务 UUID 及分配到的句柄 */
        MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        /* 特征注册完成，打印特征 UUID、定义句柄和值句柄 */
        MODLOG_DFLT(DEBUG, "registering characteristic %s with "
                    "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,   /* 特征定义属性句柄 */
                    ctxt->chr.val_handle);  /* 特征值属性句柄（用于 Notify） */
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        /* 描述符注册完成，打印描述符 UUID 及分配到的句柄 */
        MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n",
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
 * 说明：初始化并向 NimBLE 协议栈注册所有 GATT 服务。
 *       调用顺序：初始化标准服务 → 统计资源 → 注册自定义服务
 * 返回：0 成功；非零 BLE 错误码
 * ============================================================ */
int
gatt_svr_init(void)
{
    int rc;

    /* 初始化标准 GAP 服务（设备名称、外观等基础属性） */
    ble_svc_gap_init();

    /* 初始化标准 GATT 服务（服务变更特征等） */
    ble_svc_gatt_init();

    /* 初始化 Alert Notification Service（ANS，警报通知服务） */
    ble_svc_ans_init();

    /* 统计自定义服务所需的 ATT 资源数量（内存预分配） */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    /* 将自定义服务表注册到 NimBLE GATT 服务器 */
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
