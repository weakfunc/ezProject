# BLE 写特征失败排查文档

## 问题描述

App 手动点击按钮向 ESP32 写入 BLE 特征，需要反复点击多次才能成功传到 STM32。链路为：

```
Android App → BLE Write → ESP32 → UART → STM32
```

---

## 排查过程

### 第一步：定位丢失位置

用示波器抓 ESP32 的 UART TX 引脚，发现 App 发送多次后 TX 才更新波形，说明问题在 **ESP32 的 BLE 接收侧或 App 的 BLE 发送侧**，与 STM32 无关。

### 第二步：确认 ESP32 write 回调触发情况

在 `frame_parse()` 入口加 log：

```c
ESP_LOGI("BLE", "frame_parse called: %02X %02X %02X ... %02X",
         buf[0], buf[1], buf[2], buf[15]);
```

结果：**App 点击多次，frame_parse 只偶尔触发**，确认问题在 BLE 层，write 回调本身没有稳定触发。

### 第三步：排查 Android 侧

在 `onCharacteristicWrite` 回调加 status log：

```kotlin
Log.d("BLE", "onCharacteristicWrite status=$status")
```

Logcat 结果：**绝大多数返回 status=17，偶尔才有 status=0（成功）**，完全符合现象。

---

## 根本原因分析

### Bug 1：RSSI 轮询与 Write 操作竞争（Android 侧）

Android GATT 层同一时刻只允许一个 pending 操作。App 每 1500ms 调用一次 `readRemoteRssi()`，当 write 操作发出时如果 RSSI 操作还在 pending，**write 会被 Android 静默丢弃**——函数返回值看起来是成功，但数据根本没有发出。多次点击偶尔成功，是因为碰巧点在了 RSSI 操作的间隙里。

另外，`onCharacteristicWrite` 回调未实现，Write with Response 没有完成确认机制，GATT 层认为上一次写入仍在 pending，导致后续写入被拒绝。

**修复：**

```kotlin
// 1. 实现 onCharacteristicWrite 回调（必须）
override fun onCharacteristicWrite(
    gatt: BluetoothGatt,
    characteristic: BluetoothGattCharacteristic,
    status: Int
) {
    Log.d("BLE", "onCharacteristicWrite status=$status")
    mainHandler.post {
        startRssiPolling() // write 完成后再恢复 RSSI 轮询
    }
}

// 2. 写操作期间暂停 RSSI 轮询（必须）
fun writeCharacteristic(...): Boolean {
    stopRssiPolling()
    val result = writeCharacteristicCompat(gatt, characteristic, value)
    return result
}
```

### Bug 2：NimBLE mbuf 资源耗尽（ESP32 侧）

status=17 对应 ATT 协议错误码 `0x11 = ATT_ERR_INSUFFICIENT_RESOURCES`，是 **ESP32 NimBLE 栈通过 ATT_ERROR_RSP 返回给 Android 的错误**，Android 原样透传到 `onCharacteristicWrite` 的 status 参数。

NimBLE 使用 mbuf 池管理 BLE 数据包内存，默认 `MSYS_1_BLOCK_COUNT = 12`。RSSI 轮询频繁触发发包，加上 write 操作，mbuf 池被快速耗尽，后续 write request 直接被 NimBLE 拒绝返回 `0x11`。

**修复：调大 mbuf 相关配置**

在 `sdkconfig.defaults` 中：

```
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=64
CONFIG_BT_NIMBLE_ACL_BUF_COUNT=10
CONFIG_BT_NIMBLE_ACL_BUF_SIZE=251
```

### Bug 3：write 回调在 NimBLE ATT task 上下文中做耗时操作（ESP32 侧）

原始 `gatt_svc_access` 的 `WRITE_CHR` 分支直接调用 `frame_parse()`，其中包含 memcpy 和循环，在 NimBLE ATT task 上下文中执行会占用资源，导致 ATT 事件队列积压，加剧 mbuf 耗尽。

**修复：write 回调只拷贝数据，通知其他 task 处理**

```c
// 全局缓冲区和任务句柄
static uint8_t      s_write_buf[BLE_FRAME_LEN];
static TaskHandle_t s_parse_task_handle;

// write 回调：只拷贝，不解析
case BLE_GATT_ACCESS_OP_WRITE_CHR:
    if (OS_MBUF_PKTLEN(ctxt->om) != BLE_FRAME_LEN) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    {
        uint8_t  rx_buf[BLE_FRAME_LEN];
        uint16_t rx_len = 0;
        rc = ble_hs_mbuf_to_flat(ctxt->om, rx_buf, BLE_FRAME_LEN, &rx_len);
        if (rc != 0) return BLE_ATT_ERR_UNLIKELY;
        memcpy(s_write_buf, rx_buf, BLE_FRAME_LEN);
        xTaskNotifyGive(s_parse_task_handle); // 通知解析 task
    }
    return 0;

// 独立解析 task
void ble_frame_parse_task(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        frame_parse(s_write_buf);
    }
}
```

---

## 修复汇总

| 位置 | 问题 | 修复 |
|------|------|------|
| Android App | RSSI 轮询与 write 竞争，write 被静默丢弃 | write 前 `stopRssiPolling()`，`onCharacteristicWrite` 后恢复 |
| Android App | 未实现 `onCharacteristicWrite`，GATT 层认为 write 一直 pending | 实现回调，Write with Response 完成确认 |
| ESP32 NimBLE | mbuf 池默认 12 个，频繁操作导致耗尽，返回 ATT_ERR 0x11 | `MSYS_1_BLOCK_COUNT` 调至 64，ACL buffer 同步调大 |
| ESP32 NimBLE | write 回调在 ATT task 中做 frame_parse，加剧资源占用 | 回调只做 memcpy + xTaskNotifyGive，解析移到独立 task |

---

## 修复后数据流

```
App 点击按钮
  → stopRssiPolling()
  → writeCharacteristic()（Write with Response）
  → ESP32 write 回调触发
      → memcpy 到 s_write_buf
      → xTaskNotifyGive(s_parse_task_handle)
      → 回调立即返回 0（释放 NimBLE ATT task）
  → ble_frame_parse_task 醒来
      → frame_parse(s_write_buf)
      → 更新 bleInfo.bleCmdFrameArr[4..7]
  → onCharacteristicWrite(status=0) 回调
      → startRssiPolling() 恢复
  → appcom_task 5ms 周期
      → 复制到 stm32CmdFrameArr[4..7]
      → appcom_stm32_send_next_frame()
      → UART → STM32
```
