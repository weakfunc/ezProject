# APP 软件需求文档 — 智能分拣控制系统

> 本文档供 **APP 项目目录下的 Claude Code** 使用，拿到本文档 + APP 项目源码即可完成开发，无需其他文档。

---

## 一、项目概述

本 APP 为智能物品分拣系统的手机端监控与控制界面，通过 BLE（蓝牙低功耗）与 STM32 主控通信。

APP 主要功能：
- 显示传送带运行状态及当前分拣信息（包裹数量、颜色、形状、是否出站、控制舵机编号）
- 提供传送带启停控制按钮

---

## 二、开发边界

**只修改** `MainActivity.kt` 中的 `HomeScreen()` 函数，实现 UI 展示和控制交互。

**不可修改**：`BleProtocol.kt`、`BleConnectionManager.kt`、`BleAutoConnector.kt` 等通信底层文件。

- UI 使用 Jetpack Compose + Material Design 3
- 可复用已有的 `HomeSection()` 和 `ProtoFieldRow()` 组件

---

## 三、通信变量映射表

### ⚠️ 重要说明

- **0x17 帧由 ESP32 专属发送，不来自 STM32。STM32 数据从 0x18 帧开始。**
- 不要从 0x17 帧读取 STM32 数据。

---

### APP 接收（显示用）：STM32 → APP

| 数据含义 | APP RX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_TX 索引 |
|---|---|---|---|---|
| 包裹数量 | 0x18 | var4b1 | uint32 | TX[4] |
| 当前包裹是否出站（0=未出站，1=出站） | 0x18 | var4b2 | uint32 | TX[5] |
| 当前包裹控制舵机编号 | 0x18 | var1b1 | uint8 | TX[6] |
| 当前包裹颜色（0x01=白色，0x02=红色，0x03=黑色） | 0x18 | var1b2 | uint8 | TX[7] |
| 当前包裹形状（0x01=圆形，0x02=矩形） | 0x19 | var4b1 | uint32 | TX[8] |

> TX[9]~TX[15] 当前未使用，无需读取。

---

### APP 发送（控制用）：APP → STM32

| 操作含义 | APP TX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_RX 索引 |
|---|---|---|---|---|
| 传送带启停控制（1=启动，0=停止） | 0x21 | var4_1 | uint8 | RX[0] |

---

### 读取数据代码示例

```kotlin
// 读取 STM32 数据（从 0x18 开始，不要用 0x17 读 STM32 数据）
val frame18 = BleProtocol.rxFrames[0x18]
val packageCount   = frame18?.var4b1 ?: 0   // 包裹数量，对应 TX[4]
val isOut          = frame18?.var4b2 ?: 0   // 是否出站，对应 TX[5]
val servoId        = frame18?.var1b1 ?: 0   // 控制舵机编号，对应 TX[6]
val packageColor   = frame18?.var1b2 ?: 0   // 包裹颜色，对应 TX[7]

val frame19 = BleProtocol.rxFrames[0x19]
val packageShape   = frame19?.var4b1 ?: 0   // 包裹形状，对应 TX[8]
```

### 发送数据代码示例

```kotlin
// 传送带启动
fun sendConveyorControl(start: Boolean) {
    val frame = BleProtocol.buildTxFrame(
        cmd    = 0x21,
        var4_1 = if (start) 1 else 0,   // RX[0]: 1=启动，0=停止
        var4_2 = 0,
        var1_1 = 0,
        var1_2 = 0
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
}
```

---

## 四、UI 界面设计

### 顶部标题区域（强制要求）

`HomeScreen()` 顶部必须包含标题区域，显示：
- **项目名称**（大字体居中）：`智能分拣控制系统`
- **项目作者**（小字体，显示在项目名称下方）：2200810815李家毅

### 功能区域划分

使用 `HomeSection` 分区，共两个区域：

---

#### 区域一：传送带控制

包含以下控制按钮：

| 按钮文字 | 点击行为 |
|---|---|
| 启动传送带 | 发送 cmd=0x21，var4_1=1 |
| 停止传送带 | 发送 cmd=0x21，var4_1=0 |

按钮状态管理：
- 用 `var conveyorRunning by remember { mutableStateOf(false) }` 跟踪当前状态
- 两个按钮始终可见，不做 enable/disable 限制，点击即发送

---

#### 区域二：当前分拣信息

显示以下数据行（用 `ProtoFieldRow` 或 `Text` 展示）：

| 显示标签 | 数据来源 | 显示格式 |
|---|---|---|
| 包裹总数 | frame18?.var4b1 | 直接显示数字，如 `42` |
| 是否出站 | frame18?.var4b2 | `1` → `已出站`，`0` → `未出站` |
| 控制舵机 | frame18?.var1b1 | 直接显示编号，如 `2` |
| 包裹颜色 | frame18?.var1b2 | `0x01` → `白色`，`0x02` → `红色`，`0x03` → `黑色`，其他 → `未知` |
| 包裹形状 | frame19?.var4b1 | `0x01` → `圆形`，`0x02` → `矩形`，其他 → `未知` |

---

## 五、交互逻辑

- **数据刷新**：`BleProtocol.rxFrames` 是 `mutableStateOf`，Compose UI 读取时自动触发重组，无需手动刷新
- **按钮控制**：点击按钮直接调用 `sendConveyorControl(true/false)` 发送帧
- **颜色/形状显示**：在 UI 层用 `when` 表达式将枚举值转换为中文字符串后再显示

---

## 六、输出物要求

Claude Code 完成开发后，必须输出：

### 1. 代码文件

- 修改后的 `MainActivity.kt`（用注释标注修改区域的起止位置）

### 2. 功能实现文档（`app_implementation_report.md`）

**(a) 功能实现状态清单**

| 序号 | 功能描述 | 状态 | 说明 |
|---|---|---|---|
| 1 | 顶部标题区域（项目名称/作者） | ✅/⚠️/❌ | |
| 2 | 传送带启动按钮 | ✅/⚠️/❌ | |
| 3 | 传送带停止按钮 | ✅/⚠️/❌ | |
| 4 | 包裹总数显示 | ✅/⚠️/❌ | |
| 5 | 是否出站显示 | ✅/⚠️/❌ | |
| 6 | 控制舵机编号显示 | ✅/⚠️/❌ | |
| 7 | 包裹颜色显示（中文） | ✅/⚠️/❌ | |
| 8 | 包裹形状显示（中文） | ✅/⚠️/❌ | |

状态三级：✅ 已实现、⚠️ 部分实现（说明缺什么）、❌ 未实现（说明原因）

**(b) 已实现功能的测试步骤**

对每个 APP 功能，给出具体的测试方法和预期结果，让开发者可以逐项验证。
