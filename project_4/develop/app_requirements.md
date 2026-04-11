# APP 软件需求文档
# 智能控温足浴桶控制系统

---

## 一、项目概述

本文档面向 Android APP（Jetpack Compose + Material Design 3）的 Claude Code 开发者。

APP 需实现以下功能界面和交互：
- **状态显示**：实时展示当前水温、目标温度、按摩模式、加热状态、剩余定时、故障提示
- **远程控制**：APP 发送开关机、目标温度调节、按摩模式切换、定时设置指令
- 所有控制通过 Button 按钮触发，数据显示为文本
- 主页面顶部显示项目名称和作者信息

---

## 二、开发边界

**允许修改：**
- `MainActivity.kt` 中的 `HomeScreen()` 函数（UI 主界面）
- `userConfig.kt` 中的项目名称（`projectName`）和项目作者（`projectAuthor`）

**禁止修改：**
- `BleProtocol.kt`、`BleConnectionManager.kt`、`BleAutoConnector.kt` 等通信底层文件
- `BleScanActivity.kt`、`SecondActivity.kt`、`ThirdActivity.kt`、`AppBottomNavigation.kt`
- `App.kt`、`ui/theme/` 下所有文件

**技术规范：**
- UI 使用 Jetpack Compose + Material Design 3
- 可复用现有 `HomeSection()` 分区卡片组件和 `ProtoFieldRow()` 数据行组件
- 数据刷新依赖 Compose 自动重组（`rxFrames` 为 `mutableStateOf`，无需手动刷新）

---

## 三、通信变量映射表

### ⚠️ 重要说明
- **0x17 帧由 ESP32 专属发送，不来自 STM32。** APP 能读到的 STM32 上报数据从 **0x18 帧**开始，不要使用 0x17 帧的数据读取 STM32 状态。
- STM32 TX[0]~[3] 被 ESP32 截留不转发，APP 无法收到，请勿从 0x17 帧推断 STM32 数据。

### APP 接收（显示用，STM32 → APP）

| 数据含义 | APP RX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_TX 索引 |
|----------|-----------|------|----------|------------------------------|
| 当前水温（℃，float） | 0x18 | var4b1 | Float bits → Float | TX[4] |
| 目标水温（℃，整数） | 0x18 | var4b2 | UInt32 | TX[5] |
| 当前按摩模式 | 0x18 | var1b1 | UInt8（低8位） | TX[6] |
| 系统状态字节 | 0x18 | var1b2 | UInt8（位域） | TX[7] |
| 剩余定时（秒） | 0x19 | var4b1 | UInt32 | TX[8] |
| 电机PWM占空比（调试） | 0x19 | var4b2 | UInt32 | TX[9] |
| 故障码 | 0x19 | var1b1 | UInt8 | TX[10] |

**系统状态字节（TX[7]）位定义：**
| bit | 含义 |
|-----|------|
| bit0（0x01） | 系统运行中（1=运行，0=待机） |
| bit1（0x02） | 加热中（1=加热，0=停止） |
| bit2（0x04） | 干烧告警（1=缺水） |
| bit3（0x08） | 超温告警（1=超温） |

**故障码（TX[10]）：**
| 值 | 含义 |
|----|------|
| 0 | 正常 |
| 1 | 超温告警 |
| 2 | 干烧（缺水）告警 |
| 3 | 超温+干烧复合故障 |

**读取代码示例：**
```kotlin
// 读取 STM32 上报数据（从 0x18 帧开始，不用 0x17）
val frame18 = BleProtocol.rxFrames[0x18]
val frame19 = BleProtocol.rxFrames[0x19]

// 当前水温（float，保留1位小数）
val rawTemp = frame18?.var4b1 ?: 0
val currentTemp = java.lang.Float.intBitsToFloat(rawTemp)

// 目标水温（整数）
val targetTemp = frame18?.var4b2 ?: 40

// 按摩模式
val massageMode = frame18?.var1b1 ?: 0

// 系统状态字节
val statusByte = frame18?.var1b2 ?: 0
val isRunning  = (statusByte and 0x01) != 0
val isHeating  = (statusByte and 0x02) != 0
val isDryBurn  = (statusByte and 0x04) != 0
val isOverTemp = (statusByte and 0x08) != 0

// 剩余定时（秒）
val remainSec = frame19?.var4b1 ?: 0

// 故障码
val faultCode = frame19?.var1b1 ?: 0
```

### APP 发送（控制用，APP → STM32）

| 操作含义 | APP TX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_RX 索引 |
|----------|-----------|------|----------|------------------------------|
| 设定目标温度（℃，35~48） | 0x21 | var4_1 | UInt32 | RX[0] |
| 设定定时时长（分钟，0~60） | 0x21 | var4_2 | UInt32 | RX[1] |
| 按摩模式指令 | 0x21 | var1_1 | UInt8 | RX[2] |
| 系统开关指令 | 0x21 | var1_2 | UInt8 | RX[3] |

**所有控制指令复用同一帧（0x21），每次发送时携带当前全部控制参数。**

**发送代码示例：**
```kotlin
// 发送控制帧（每次发送携带所有当前控制值）
fun sendControlFrame(
    targetTemp: Int,    // 目标温度 35~48
    timerMin: Int,      // 定时分钟 0~60
    massageMode: Int,   // 按摩模式 0~3
    powerOn: Int        // 开关 0/1
) {
    val frame = BleProtocol.buildTxFrame(
        cmd     = 0x21,
        var4_1  = targetTemp,
        var4_2  = timerMin,
        var1_1  = massageMode,
        var1_2  = powerOn
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

### 4.1 userConfig.kt 配置

```kotlin
val projectName   = "智能控温足浴桶的设计与实现"
val projectAuthor = "黄琳"
```

### 4.2 整体布局

`HomeScreen()` 从上到下依次为：
1. **顶部标题区**（固定，不在 ScrollColumn 内）
2. **系统状态区** — 显示当前水温、目标温度、加热状态、运行状态
3. **温度控制区** — 目标温度升降按钮
4. **按摩模式区** — 模式选择按钮
5. **定时控制区** — 定时时长选择按钮
6. **电源控制区** — 开机/关机按钮
7. **故障提示区** — 故障码和告警状态（仅故障时显示，或常显）

### 4.3 顶部标题区（强制要求）

```kotlin
Column(
    modifier = Modifier.fillMaxWidth().padding(16.dp),
    horizontalAlignment = Alignment.CenterHorizontally
) {
    Text(
        text = UserConfig.projectName,
        style = MaterialTheme.typography.headlineSmall,
        fontWeight = FontWeight.Bold,
        textAlign = TextAlign.Center
    )
    Text(
        text = UserConfig.projectAuthor,
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant
    )
}
```

### 4.4 系统状态区

用 `HomeSection(title = "系统状态")` 包裹，内部用 `ProtoFieldRow` 显示：

| 显示内容 | 格式示例 |
|----------|----------|
| 当前水温 | `当前水温：38.5℃` |
| 目标温度 | `目标温度：40℃` |
| 加热状态 | `加热状态：加热中` / `未加热` |
| 运行状态 | `系统状态：运行中` / `待机` |
| 剩余定时 | `剩余定时：05:30` / `未设定` |

剩余定时显示格式：`timerMin:timerSec`，用 `String.format("%02d:%02d", remainSec/60, remainSec%60)` 格式化。

### 4.5 温度控制区

用 `HomeSection(title = "温度设定")` 包裹：
- 显示当前目标温度：`设定温度：40℃`
- 按钮：`[温度 -1℃]` `[温度 +1℃]`
- 额外快捷按钮：`[35℃]` `[38℃]` `[40℃]` `[42℃]` `[45℃]`
- 点击任何温度按钮后，立即调用 `sendControlFrame()` 发送完整控制帧

温度范围约束：本地 state 变量 `targetTempState` 修改时，clamp 到 [35, 48]。

### 4.6 按摩模式区

用 `HomeSection(title = "按摩模式")` 包裹：
- 当前模式文本：`当前模式：标准档`（根据 massageMode 值显示中文名）
- 四个按钮（等宽排列）：`[停止]` `[轻柔]` `[标准]` `[强劲]`
- 点击后更新本地 `massageModeState`，立即发送控制帧

按摩模式中文映射：
```kotlin
val modeNames = listOf("停止", "轻柔", "标准", "强劲")
// 显示 STM32 上报的当前模式
val currentModeText = modeNames.getOrElse(massageMode) { "未知" }
```

### 4.7 定时控制区

用 `HomeSection(title = "定时设置")` 包裹：
- 显示当前定时设置：`定时时长：30分钟` / `未设定`
- 快捷按钮：`[不定时]` `[15分钟]` `[30分钟]` `[45分钟]` `[60分钟]`
- 点击后更新本地 `timerMinState`，立即发送控制帧

### 4.8 电源控制区

用 `HomeSection(title = "电源控制")` 包裹：
- 一个大按钮，根据运行状态切换文字和颜色：
  - 待机时：`[开机]`，绿色（`MaterialTheme.colorScheme.primary`）
  - 运行时：`[关机]`，红色（`MaterialTheme.colorScheme.error`）
- 点击后反转本地 `powerState`（0→1 或 1→0），立即发送控制帧

### 4.9 故障提示区

用 `HomeSection(title = "设备状态")` 包裹：
- 常显故障码和告警状态：
  - `ProtoFieldRow(label1 = "超温告警", value1 = if(isOverTemp) "⚠ 超温" else "正常", label2 = "干烧告警", value2 = if(isDryBurn) "⚠ 缺水" else "正常")`
  - 若 `faultCode != 0`：用红色 Text 显示 `故障：$faultText`
- 故障码文本映射：
  ```kotlin
  val faultText = when(faultCode) {
      1 -> "超温保护"
      2 -> "缺水保护"
      3 -> "超温+缺水"
      else -> "正常"
  }
  ```
- 告警时，对应显示值的文字颜色改为 `MaterialTheme.colorScheme.error`

---

## 五、交互逻辑

### 5.1 本地状态变量

在 `HomeScreen()` 中用 `remember { mutableStateOf(...) }` 维护以下本地状态：

```kotlin
var targetTempState by remember { mutableStateOf(40) }   // 目标温度（本地操作值）
var massageModeState by remember { mutableStateOf(0) }   // 按摩模式（本地操作值）
var timerMinState by remember { mutableStateOf(0) }      // 定时分钟（本地操作值）
var powerState by remember { mutableStateOf(0) }         // 开关（本地操作值）
```

### 5.2 数据刷新

- 从 `BleProtocol.rxFrames` 读取 STM32 数据（Compose 自动重组，无需手动刷新）
- 本地状态变量用于按钮操作的即时反馈；STM32 回传的实际状态用于状态栏显示
- 系统运行状态（`isRunning`）从 STM32 TX[7] bit0 读取，用于电源按钮状态显示

### 5.3 发送时机

每次用户点击控制按钮后，**立即**调用一次 `sendControlFrame()`，携带当时所有控制参数的当前值。

### 5.4 按钮状态管理

- 电源按钮：文字和颜色根据 `isRunning`（STM32 上报的真实运行状态）切换，不依赖本地 `powerState`
- 模式按钮：当前选中模式按钮可设置不同背景色（如 `filled` vs `outlined` 风格）以区分选中态
- 无 BLE 连接时，按钮保持可点击（发送失败静默处理，不弹 Toast），但显示值显示为 `--`

### 5.5 异常状态 UI

- `faultCode != 0`：在故障区显示红色故障文本
- `isDryBurn == true`：干烧告警字段文字变红
- `isOverTemp == true`：超温告警字段文字变红
- `currentTemp` 显示为 1 位小数，使用 `String.format("%.1f", currentTemp)` 格式化

---

## 六、输出物要求

Claude Code 完成开发后，必须输出：

### 1. 代码文件
- 修改后的 `MainActivity.kt`（在修改区域前后加注释 `// === 修改开始 ===` / `// === 修改结束 ===`）

### 2. 功能实现文档（`app_implementation_report.md`）

**(a) 功能实现状态清单：**

| 序号 | 功能描述 | 状态 | 说明 |
|------|----------|------|------|
| 1 | 项目名称/作者顶部显示 | ✅/⚠️/❌ | |
| 2 | 当前水温实时显示 | ✅/⚠️/❌ | |
| 3 | 目标温度显示与调节按钮 | ✅/⚠️/❌ | |
| 4 | 按摩模式显示与切换按钮 | ✅/⚠️/❌ | |
| 5 | 定时设置按钮 | ✅/⚠️/❌ | |
| 6 | 开关机按钮（状态联动） | ✅/⚠️/❌ | |
| 7 | 故障/告警状态显示 | ✅/⚠️/❌ | |
| 8 | 剩余定时倒计时显示 | ✅/⚠️/❌ | |

**(b) 已实现功能的测试步骤：**

对每个 APP 功能，给出具体操作步骤和预期 UI 结果，例如：
- 测试当前水温显示：STM32 上报 TX[4]=38.5f → APP 显示"当前水温：38.5℃"
- 测试开机按钮：点击[开机] → 发送 powerOn=1 帧 → STM32 状态字节 bit0=1 → 按钮变为红色[关机]
