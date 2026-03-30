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
 *   BLE 外设主程序入口，负责以下功能：
 *   1. NVS（非易失性存储）初始化
 *   2. NimBLE 协议栈初始化与配置
 *   3. GAP 事件处理（连接、断开、加密变化等）
 *   4. BLE 广播控制
 *   5. 启动 NimBLE FreeRTOS 后台任务
 * ============================================================ */

#include "freertos/FreeRTOS.h"                /* FreeRTOS 基础定义 */
#include "freertos/task.h"                    /* xTaskCreate 等任务接口 */
#include "esp_log.h"                          /* ESP-IDF 日志接口 */
#include "esp_timer.h"                        /* esp_timer_get_time（微秒级系统时间）*/
#include <inttypes.h>                         /* PRIu32 等格式化宏 */
#include "nvs_flash.h"                        /* NVS 非易失性存储接口 */
#include "nimble/nimble_port.h"               /* NimBLE 协议栈端口层 */
#include "nimble/nimble_port_freertos.h"      /* NimBLE FreeRTOS 适配层 */
#include "host/ble_hs.h"                      /* NimBLE 主机层 */
#include "host/util/util.h"                   /* NimBLE 主机层工具函数（print_addr 等） */
#include "services/gap/ble_svc_gap.h"         /* GAP 标准服务 */
#include "driver_ble.h"                       /* BLE 驱动模块（GATT 服务、UUID、函数声明） */
#include "driver_stm32.h"                     /* STM32 串口驱动模块 */
#include "task_appcom.h"                      /* APP 通信数据流映射任务 */
#include "main.h"                             /* systemConfig_t 公共声明 */

/* systemConfig_t 定义见 main.h */
systemConfig_t systemConfig = {
    .sys_time_s = 0,
};

/* 日志标签，用于 ESP_LOG 输出前缀 */
static const char *tag = "NimBLE_BLE_PRPH";

#define NIMBLE_TERMINAL_LOG_ENABLED 0

#if NIMBLE_TERMINAL_LOG_ENABLED
#define NIMBLE_MODLOG(...) MODLOG_DFLT(__VA_ARGS__)
#define NIMBLE_ESP_LOGI(...) ESP_LOGI(__VA_ARGS__)
#define NIMBLE_ESP_LOGE(...) ESP_LOGE(__VA_ARGS__)
static inline void nimble_print_addr(const uint8_t *addr)
{
    print_addr(addr);
}
#else
#define NIMBLE_MODLOG(...) ((void)0)
#define NIMBLE_ESP_LOGI(...) ((void)0)
#define NIMBLE_ESP_LOGE(...) ((void)0)
static inline void nimble_print_addr(const uint8_t *addr)
{
    (void)addr;
}
#endif

/* GAP 事件回调函数前向声明 */
#undef MODLOG_DFLT
#define MODLOG_DFLT(...) NIMBLE_MODLOG(__VA_ARGS__)

#undef print_addr
#define print_addr(...) nimble_print_addr(__VA_ARGS__)

static int bleprph_gap_event(struct ble_gap_event *event, void *arg);

/* 广播时使用的本机地址类型（公共地址或随机地址），由 ble_hs_id_infer_auto() 推断 */
static uint8_t own_addr_type;

/* NimBLE 持久化存储配置初始化（实现位于 NimBLE 组件内） */
void ble_store_config_init(void);

/* ============================================================
 * 函数：bleprph_print_conn_desc
 * 说明：将连接描述信息（地址、连接参数、安全状态）打印到日志
 * 参数：desc —— 当前连接的描述结构体指针
 * ============================================================ */
static void
bleprph_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    MODLOG_DFLT(INFO, "handle=%d our_ota_addr_type=%d our_ota_addr=",
                desc->conn_handle, desc->our_ota_addr.type);
    print_addr(desc->our_ota_addr.val);   /* 本机空中地址（OTA） */
    MODLOG_DFLT(INFO, " our_id_addr_type=%d our_id_addr=",
                desc->our_id_addr.type);
    print_addr(desc->our_id_addr.val);    /* 本机身份地址（ID） */
    MODLOG_DFLT(INFO, " peer_ota_addr_type=%d peer_ota_addr=",
                desc->peer_ota_addr.type);
    print_addr(desc->peer_ota_addr.val);  /* 对端空中地址 */
    MODLOG_DFLT(INFO, " peer_id_addr_type=%d peer_id_addr=",
                desc->peer_id_addr.type);
    print_addr(desc->peer_id_addr.val);   /* 对端身份地址 */
    MODLOG_DFLT(INFO, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl,                    /* 连接间隔（单位 1.25ms） */
                desc->conn_latency,                 /* 从设备延迟（允许跳过的连接事件数） */
                desc->supervision_timeout,          /* 监督超时（单位 10ms） */
                desc->sec_state.encrypted,          /* 当前链路是否已加密 */
                desc->sec_state.authenticated,      /* 当前链路是否已认证 */
                desc->sec_state.bonded);            /* 是否已建立绑定 */
}

/* ============================================================
 * 函数：bleprph_advertise
 * 说明：配置并启动 BLE 广播
 *       广播模式：通用可发现（General Discoverable）+ 无定向可连接（Undirected Connectable）
 * ============================================================ */
static void
bleprph_advertise(void)
{
    struct ble_gap_adv_params adv_params; /* 广播参数（连接模式、发现模式） */
    struct ble_hs_adv_fields fields;      /* 广播数据字段（名称、UUID、标志等） */
#if CONFIG_BT_NIMBLE_GAP_SERVICE
    const char *name;                     /* 设备名称指针 */
#endif
    int rc;

    /* 清零广播字段，确保未设置的字段为默认值 */
    memset(&fields, 0, sizeof fields);

    /* 设置广播标志位：
     *   BLE_HS_ADV_F_DISC_GEN   —— 通用可发现模式
     *   BLE_HS_ADV_F_BREDR_UNSUP —— 仅支持 BLE，不支持经典蓝牙（BR/EDR）
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* 在广播包中包含 TX 功率级别字段，值由协议栈自动填充 */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    /* 在广播包中包含完整设备名称 */
    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;          /* 名称为完整名称（非缩略） */
#endif

    /* 在广播包中声明 Alert Notification Service 的 16 位 UUID（0x1811）
     * 便于扫描方识别本设备支持的服务 */
    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(GATT_SVR_SVC_ALERT_UUID)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;       /* UUID 列表完整（无更多 16 位 UUID） */

    /* 将广播数据写入协议栈 */
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    /* 配置广播连接模式和发现模式，然后启动广播 */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  /* 无定向可连接 */
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  /* 通用可发现 */
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, bleprph_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
}

/* ============================================================
 * 函数：bleprph_gap_event
 * 说明：GAP 事件回调函数，由 NimBLE 协议栈在发生 GAP 事件时调用。
 *       本工程对所有连接共用同一个回调函数。
 * 参数：event —— 事件类型及相关数据
 *       arg   —— 用户自定义参数（本工程中未使用）
 * 返回：0 表示处理成功；非零表示失败
 * ============================================================ */
static int
bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc; /* 用于获取连接详细信息 */
    int rc;

    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        /* ---- 连接事件：新连接建立或连接尝试失败 ---- */
        MODLOG_DFLT(INFO, "connection %s; status=%d ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0) {
            /* 连接成功，查询并打印连接描述信息 */
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            bleprph_print_conn_desc(&desc);
            driver_ble_on_connect(event->connect.conn_handle);
        }
        MODLOG_DFLT(INFO, "\n");

        if (event->connect.status != 0) {
            /* 连接失败，重新开始广播以等待新的连接 */
            bleprph_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* ---- 断连事件：打印原因并重新广播 ---- */
        MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
        bleprph_print_conn_desc(&event->disconnect.conn);
        MODLOG_DFLT(INFO, "\n");

        driver_ble_on_disconnect();
        /* 连接已断开，重新开始广播 */
        bleprph_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* ---- 连接参数更新事件：中心设备请求更新连接参数 ---- */
        MODLOG_DFLT(INFO, "connection updated; status=%d ",
                    event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        MODLOG_DFLT(INFO, "\n");
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        /* ---- 广播完成事件：广播超时或被中止，重新开始广播 ---- */
        MODLOG_DFLT(INFO, "advertise complete; reason=%d",
                    event->adv_complete.reason);
        bleprph_advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* ---- 加密状态变化事件：连接的加密状态发生改变 ---- */
        MODLOG_DFLT(INFO, "encryption change event; status=%d ",
                    event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        MODLOG_DFLT(INFO, "\n");
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        /* ---- 通知发送事件：GATT Notification/Indication 发送完成 ---- */
        MODLOG_DFLT(INFO, "notify_tx event; conn_handle=%d attr_handle=%d "
                    "status=%d is_indication=%d",
                    event->notify_tx.conn_handle,
                    event->notify_tx.attr_handle,
                    event->notify_tx.status,
                    event->notify_tx.indication);  /* 1=Indication, 0=Notification */
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        /* ---- 订阅事件：对端设备通过 CCCD 订阅或取消订阅通知/指示 ---- */
        MODLOG_DFLT(INFO, "subscribe event; conn_handle=%d attr_handle=%d "
                    "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                    event->subscribe.conn_handle,
                    event->subscribe.attr_handle,
                    event->subscribe.reason,
                    event->subscribe.prev_notify,
                    event->subscribe.cur_notify,
                    event->subscribe.prev_indicate,
                    event->subscribe.cur_indicate);
        driver_ble_on_subscribe(event->subscribe.attr_handle,
                                (bool)event->subscribe.cur_notify);
        return 0;

    case BLE_GAP_EVENT_MTU:
        /* ---- MTU 协商事件：ATT 最大传输单元协商完成 ---- */
        MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);  /* 协商后的 MTU 值（字节） */
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* ---- 重复配对事件：对端已有绑定信息但尝试重新配对 ----
         * 策略：删除旧绑定信息并接受新配对（牺牲安全性换取便利性） */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr); /* 删除旧绑定记录 */
        /* 返回 RETRY 指示协议栈继续配对流程 */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        /* ---- 配对密钥交互事件：安全管理器需要处理密钥输入/显示 ----
         * 本工程仅支持"显示固定密钥"方式，其他 I/O 能力不处理 */
        NIMBLE_ESP_LOGI(tag, "PASSKEY_ACTION_EVENT started");
        {
            struct ble_sm_io pkey = {0};

            if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
                /* 设备显示固定配对码，对端用户在手机 APP 输入此码 */
                pkey.action = event->passkey.params.action;
                pkey.passkey = 123456;  /* 固定配对码：123456 */
                NIMBLE_ESP_LOGI(tag, "Enter passkey %" PRIu32 " on the peer side", pkey.passkey);
                rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
                NIMBLE_ESP_LOGI(tag, "ble_sm_inject_io result: %d", rc);
            }
        }
        return 0;

    case BLE_GAP_EVENT_AUTHORIZE:
        /* ---- 属性访问授权事件：默认拒绝所有需要授权的访问 ---- */
        MODLOG_DFLT(INFO, "authorize event: conn_handle=%d attr_handle=%d is_read=%d",
                    event->authorize.conn_handle,
                    event->authorize.attr_handle,
                    event->authorize.is_read);
        event->authorize.out_response = BLE_GAP_AUTHORIZE_REJECT; /* 拒绝授权 */
        return 0;
    }

    return 0;
}

/* ============================================================
 * 函数：bleprph_on_reset
 * 说明：NimBLE 协议栈复位回调，在协议栈发生异常复位时被调用
 * 参数：reason —— 复位原因代码
 * ============================================================ */
static void
bleprph_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

/* ============================================================
 * 函数：bleprph_on_sync
 * 说明：NimBLE 协议栈同步回调，在协议栈与控制器完成同步后被调用。
 *       在此确定广播地址并开始 BLE 广播。
 * ============================================================ */
static void
bleprph_on_sync(void)
{
    int rc;

    /* 确保已设置有效的身份地址，0 表示优先使用公共地址 */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* 根据当前可用地址类型自动推断广播使用的地址类型（公共/随机） */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* 读取并打印本机蓝牙地址 */
    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    MODLOG_DFLT(INFO, "Device Address: ");
    print_addr(addr_val);
    MODLOG_DFLT(INFO, "\n");

    /* 协议栈就绪，开始 BLE 广播 */
    bleprph_advertise();
}

/* ============================================================
 * 函数：bleprph_host_task
 * 说明：NimBLE 协议栈主任务，在独立的 FreeRTOS 任务中运行事件循环。
 *       仅当 nimble_port_stop() 被调用时此函数才返回。
 * 参数：param —— FreeRTOS 任务参数（未使用）
 * ============================================================ */
void bleprph_host_task(void *param)
{
    NIMBLE_ESP_LOGI(tag, "BLE Host Task Started");
    /* 运行 NimBLE 事件循环（阻塞直到协议栈停止） */
    nimble_port_run();

    /* 事件循环退出后，反初始化 FreeRTOS 适配层 */
    nimble_port_freertos_deinit();
}

/* ============================================================
 * 函数：system_time_task
 * 说明：每秒更新并打印系统运行时间（自上电起的秒数）。
 * ============================================================ */
static void system_time_task(void *arg)
{
    (void)arg;
    while (1) {
        systemConfig.sys_time_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        uint32_t stm32_time = stm32Info.stm32CmdFrameArr[0].payload.var_4b_1;
        ESP_LOGI(tag, "sys_time: %" PRIu32 " s | stm32_time: %" PRIu32 " s",
                 systemConfig.sys_time_s, stm32_time);
 //       appcom_dbg_print_rx();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ============================================================
 * 函数：app_main
 * 说明：ESP-IDF 应用程序入口，完成所有初始化并启动 BLE 服务。
 *       初始化顺序：NVS → NimBLE → GATT 服务 → 设备名称 → 持久化存储 → 启动任务
 * ============================================================ */
void
app_main(void)
{
    int rc;

    /* ---- 1. 初始化 NVS（用于保存 BLE 射频校准数据及绑定记录）---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS 分区已满或固件版本升级导致格式不匹配，擦除后重新初始化 */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ---- 2. 初始化 NimBLE 协议栈端口层 ---- */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        NIMBLE_ESP_LOGE(tag, "Failed to init nimble %d ", ret);
        return;
    }

    /* ---- 3. 配置 NimBLE 主机层回调函数 ---- */
    ble_hs_cfg.reset_cb = bleprph_on_reset;           /* 协议栈复位时调用 */
    ble_hs_cfg.sync_cb = bleprph_on_sync;             /* 协议栈就绪时调用（开始广播） */
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb; /* GATT 服务/特征注册时调用 */
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr; /* 持久化存储状态变化时调用 */

    /* ---- 4. 配置安全管理器（SM）参数 ---- */
    /* I/O 能力：由 menuconfig 中的 EXAMPLE_IO_TYPE 配置（默认 No I/O） */
    ble_hs_cfg.sm_io_cap = CONFIG_EXAMPLE_IO_TYPE;

#ifdef CONFIG_EXAMPLE_BONDING
    /* 启用绑定功能：配对后保存长期密钥（LTK），断电重连时无需重新配对 */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;    /* 分发本机加密密钥 */
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;  /* 接受对端加密密钥 */
#endif

#ifdef CONFIG_EXAMPLE_MITM
    /* 启用中间人攻击（MITM）防护，要求认证配对 */
    ble_hs_cfg.sm_mitm = 1;
#endif

#ifdef CONFIG_EXAMPLE_USE_SC
    /* 启用 BLE 4.2 安全连接（Secure Connections）特性，使用 ECDH 密钥交换 */
    ble_hs_cfg.sm_sc = 1;
#else
    ble_hs_cfg.sm_sc = 0;  /* 使用传统配对方式 */
#endif

    /* ---- 5. 初始化 GATT 服务，并启动 BLE 帧解析任务 ---- */
#if MYNEWT_VAL(BLE_GATTS)
    rc = gatt_svr_init();  /* 注册所有自定义 GATT 服务和特征 */
    assert(rc == 0);
    driver_ble_start_parse_task();  /* write 回调解耦：parse 在独立任务执行 */
#endif

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    /* ---- 6. 设置 BLE 设备名称（将出现在广播包中，供扫描方识别）---- */
    rc = ble_svc_gap_device_name_set("ESP32C3_FINDME");
    assert(rc == 0);
#endif

    /* ---- 7. 初始化 BLE 持久化存储（用于保存绑定密钥等信息）---- */
    ble_store_config_init();

    /* ---- 8. 初始化 STM32 串口驱动，并启动串口接收任务 ---- */
    driver_stm32_init();
    xTaskCreate(driver_stm32_rx_task, "stm32_rx", 2048, NULL, 5, NULL);

    /* ---- 9. 启动系统时间打印任务 ---- */
    xTaskCreate(system_time_task, "sys_time", 2048, NULL, 4, NULL);

    /* ---- 10. 启动 APP 通信数据流映射任务 ---- */
    xTaskCreate(appcom_task, "appcom", 2048, NULL, 4, NULL);

    /* ---- 11. 启动 NimBLE FreeRTOS 任务，在独立任务中运行协议栈事件循环 ---- */
    nimble_port_freertos_init(bleprph_host_task);
}
