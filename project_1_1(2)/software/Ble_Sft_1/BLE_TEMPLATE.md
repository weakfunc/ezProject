# Android BLE 上位机模板说明

## 项目概述

Android BLE 上位机（Jetpack Compose），配套下位机：**ESP32-C3 + ESP-IDF + NimBLE**。

已实现功能：BLE 扫描、自动连接、GATT 服务发现、特征订阅(Notify/Indicate)、特征读写、RSSI 实时刷新、BLE 终端日志。

---

## 关键文件职责

| 文件 | 职责 |
|------|------|
| `userConfig.kt` | 所有可配置项（项目名、作者、设备 MAC、UUID、开发者模式开关等） |
| `BleProtocol.kt` | 帧格式定义、RX 帧解析（存入 `rxFrames`）、TX 帧构建 |
| `BleConnectionManager.kt` | BLE 连接/断开、特征读写/订阅、RSSI 轮询、终端日志 |
| `MainActivity.kt` | 主页面 UI（`HomeScreen` Composable） |
| `BleScanActivity.kt` | BLE 扫描页面（开发者模式可见） |

---

## BLE 帧协议

### 帧格式（固定 16 字节）

```
[0]   [1]   [2]   [3~6]  [7~10] [11]   [12]   [13]  [14]  [15]
0x55  0xAA  CMD   4B_1   4B_2   1B_1   1B_2   CRC8  CNT   0xFF
```

- `4B_1 / 4B_2`：32位整数，**小端序（Little-Endian）**
- `1B_1 / 1B_2`：单字节无符号整数
- CRC8 当前固定 0x00（预留）
- CNT：发送计数，自动递增

### CMD 范围

| 方向 | CMD 范围 | 说明 |
|------|----------|------|
| 下位机 → APP（RX） | `0x17 ~ 0x20` | 通过 Notify/Indicate 推送，解析后存入 `BleProtocol.rxFrames` |
| APP → 下位机（TX） | `0x21 ~ 0x24` | 通过 Write 发送，用 `BleProtocol.buildTxFrame()` 构建 |

---

## 数据读写接口

### 读取 RX 数据（下位机 → APP）

```kotlin
val frame = BleProtocol.rxFrames[CMD值]   // BleRxFrame? (Compose State，自动触发重组)
val value = frame?.var4b1 ?: 0            // 4B_1，Int，小端已自动解析
val value = frame?.var4b2 ?: 0            // 4B_2
val value = frame?.var1b1 ?: 0            // 1B_1
val value = frame?.var1b2 ?: 0            // 1B_2
val count = frame?.cnt    ?: 0            // 该 CMD 累计收到帧数
```

`BleRxFrame` 定义：
```kotlin
data class BleRxFrame(
    val var4b1: Int = 0,
    val var4b2: Int = 0,
    val var1b1: Int = 0,
    val var1b2: Int = 0,
    val cnt:    Int = 0
)
```

### 发送 TX 数据（APP → 下位机）

```kotlin
val frame = BleProtocol.buildTxFrame(
    cmd    = 0x21,      // TX CMD，范围 0x21~0x24
    var4_1 = 0,         // 4B_1，可选
    var4_2 = 0,         // 4B_2，可选
    var1_1 = 0x01,      // 1B_1，可选
    var1_2 = 0          // 1B_2，可选
)
val ok = BleConnectionManager.writeCharacteristic(
    serviceUuid        = UserConfig.esp32_service_1_uuid,
    characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
    value              = frame
)
if (ok) BleConnectionManager.recordOutgoingMessage(
    characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
    value              = frame
)
```

---

## UserConfig 常用配置项

```kotlin
const val DEVELOPER_MODE = false          // true=开发者模式（显示底部导航栏），false=正式模式
var project_name   = "项目名称"
var author_name    = "学号-姓名"
var esp32_device_name = "ESP32C3_FINDME"  // 自动连接目标设备名（模糊匹配）
var esp32_device_mac  = "XX:XX:XX:XX:XX:XX"  // 目标设备 MAC
var auto_connect_my_ble_device = true     // 是否自动连接
var auto_config_ble_device     = true     // 是否自动订阅特征

// 主要 BLE UUID（与下位机固件一致）
var esp32_service_1_uuid                  = "EE260001-..."
var esp32_service_1_characteristic_1_uuid = "EE260101-..."
```

---

## 主页面 UI 结构（HomeScreen）

非开发者模式下，`HomeScreen` 按以下卡片顺序排列：

1. 作者信息（顶部文字，`bodyLarge`）
2. **BLE 连接状态**卡片（始终显示）
3. *(以下仅在已连接时显示)*
4. **系统状态**卡片
5. **包裹信息**卡片
6. **传送带控制**卡片（含启动/停止按钮）

卡片使用 `HomeSection(title)` 组件包裹，内部为 `Column` + `HorizontalDivider` 分隔各行。

---

## 任务描述

```
## 项目概述
Android BLE 上位机工程，配套下位机：ESP32-C3 + ESP-IDF + NimBLE。

## 任务
完成非开发者模式下的主页面UI设计。

项目名称：XXX（醒目）
作者：学号-姓名

## TX 数据定义（APP → 下位机，写特征）
CMD: 0xXX
4B_1: 含义
1B_1: 含义（如：0x01=启动，0x00=停止）

## RX 数据定义（下位机 → APP，Notify）
CMD: 0xXX
4B_1: 含义
4B_2: 含义（如：0=未使能，1=使能）
1B_1: 含义
1B_2: 含义

CMD: 0xXX
...

## 设计原则
简洁，大学本科毕业生水平。
上述数据内容已通过调试界面验证，理论上只需要做主页面UI，正常情况下不需要改任何后端已有逻辑。
```

---

## 注意事项

- 所有 4B 字段均为**小端序**（ESP32 默认），`BleProtocol.parseInt32LE` 已正确处理
- RX CMD 范围当前为 `0x17~0x20`，如需扩展修改 `BleProtocol.CMD_RX_MAX`
- TX 与 RX 可以使用相同 CMD 值（方向相反，不冲突）
- `DEVELOPER_MODE = true` 时显示底部导航栏（扫描/终端/设置页），用于开发调试
- `DEVELOPER_MODE = false` 时只显示主页面，自动扫描并连接配置的设备
