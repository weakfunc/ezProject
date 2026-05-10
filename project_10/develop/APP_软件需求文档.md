# APP 软件需求文档 — 宠物智能饲喂系统

> 本文档供 APP 项目目录下的 Claude Code 阅读并执行开发。文档自包含，不需要参考其他资料。

---

## 一、项目概述

本项目为**宠物智能饲喂系统**配套的 Android APP。APP 通过 BLE（已由底层完成）与 STM32 设备通信，实现：

1. 实时显示设备状态（食槽余量、投喂状态、当日投喂次数、当前执行方案、报警信息）
2. 投喂计划管理（新建/修改/删除最多 5 个方案，每个方案包含 频率 + 重量）
3. 手动远程控制（启用/禁用计划执行、手动开关舵机）
4. 报警阈值设置
5. 主动选择当前执行方案（覆盖 STM32 的 RFID 识别）
6. 异常报警显示（食槽余量不足 / 电机卡堵 / 外部断电）
7. 显示当前 RFID 识别到的宠物 ID

---

## 二、开发边界

**Claude Code 只允许修改以下文件**：

- `app/src/main/java/com/example/demo_1/MainActivity.kt` 中的 `HomeScreen()` 函数
- `app/src/main/java/com/example/demo_1/userConfig.kt` 中的项目名称和项目作者字段（如该字段已存在；不存在则在 `MainActivity.kt` 中以常量方式定义）

**禁止修改的文件**（最高优先级）：

- `BleProtocol.kt`、`BleConnectionManager.kt`、`BleAutoConnector.kt`、`BleScanActivity.kt`
- `SecondActivity.kt`、`ThirdActivity.kt`、`AppBottomNavigation.kt`、`App.kt`
- `ui/theme/` 下所有文件

**技术栈约束**：

- UI：Jetpack Compose + Material Design 3
- 数据读取：`BleProtocol.rxFrames`（`mutableStateOf`，自动重组）
- 数据发送：`BleProtocol.buildTxFrame()` + `BleConnectionManager.writeCharacteristic()`
- **优先复用**已有 UI 组件：`HomeSection()`、`ProtoFieldRow()`

---

## 三、通信变量映射表

> ⚠️ **APP RX 0x17 帧由 ESP32 专属发送，不来自 STM32**。读 STM32 数据必须从 0x18 帧开始（对应 STM32 的 TX[4]~[7]）。**禁止从 0x17 帧读取任何 STM32 业务数据**。

### APP 接收（显示用）

| 数据含义 | APP RX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_TX 索引 |
|---|---|---|---|---|
| 当前食槽重量（g） | 0x18 | var4b1 | float | TX[4] |
| 当前报警阈值（g，回显） | 0x18 | var4b2 | float | TX[5] |
| 投喂状态（0=空闲，1=投喂中） | 0x18 | var1b1 | uint8 | TX[6] |
| 报警标志位（bit0=余量不足, bit1=电机卡堵, bit2=外部断电） | 0x18 | var1b2 | uint8 | TX[7] |
| 当日投喂次数 | 0x19 | var4b1 | uint32 | TX[8] |
| 系统时间戳（秒） | 0x19 | var4b2 | uint32 | TX[9] |
| 当前执行方案ID（0=无，1~5） | 0x19 | var1b1 | uint8 | TX[10] |
| 计划执行开关（0=禁用，1=启用） | 0x19 | var1b2 | uint8 | TX[11] |
| 计划广播-频率（秒） | 0x1A | var4b1 | float | TX[12] |
| 计划广播-重量（克） | 0x1A | var4b2 | float | TX[13] |
| 计划广播-当前广播方案ID（0=空槽位） | 0x1A | var1b1 | uint8 | TX[14] |
| 方案总数（固定 5） | 0x1A | var1b2 | uint8 | TX[15] |

### APP 发送（控制用）

| 操作含义 | APP TX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_RX 索引 |
|---|---|---|---|---|
| 命令参数：投喂频率（秒） | 0x21 | var4_1 | float | RX[0] |
| 命令参数：投喂重量（克） | 0x21 | var4_2 | float | RX[1] |
| 命令码（cmdCode） | 0x21 | var1_1 | uint8 | RX[2] |
| 命令目标方案ID（cmdPlanId, 1~5） | 0x21 | var1_2 | uint8 | RX[3] |
| 报警阈值参数（仅命令码=8时有效） | 0x22 | var4_1 | float | RX[4] |
| (保留) | 0x22 | var4_2 | — | RX[5] |
| 命令序列号（cmdSeq） | 0x22 | var1_1 | uint8 | RX[6] |
| (保留) | 0x22 | var1_2 | — | RX[7] |

### 命令码（cmdCode）定义

| 码值 | 含义 | 必填字段 |
|---|---|---|
| 0 | 无操作 | — |
| 1 | 新增/修改方案 | 0x21 var4_1=频率, var4_2=重量, var1_2=方案ID |
| 2 | 删除方案 | 0x21 var1_2=方案ID |
| 3 | 手动开启舵机（仅计划禁用时有效） | — |
| 4 | 手动关闭舵机（仅计划禁用时有效） | — |
| 5 | 启用计划执行 | — |
| 6 | 禁用计划执行 | — |
| 7 | 选择当前执行方案 | 0x21 var1_2=方案ID |
| 8 | 设置报警阈值 | 0x22 var4_1=阈值 |

### 数据读取代码示例

```kotlin
// 读取 STM32 状态（注意从 0x18 开始，0x17 为 ESP32 专属）
val frame18 = BleProtocol.rxFrames[0x18]
val frame19 = BleProtocol.rxFrames[0x19]
val frame1A = BleProtocol.rxFrames[0x1A]

// frame18: 重量、阈值、投喂状态、报警标志
val currentWeightBits = frame18?.var4b1 ?: 0
val currentWeight = Float.fromBits(currentWeightBits)   // 当前食槽重量（g）
val thresholdBits = frame18?.var4b2 ?: 0
val alarmThreshold = Float.fromBits(thresholdBits)      // 报警阈值（g）
val feedingState = frame18?.var1b1 ?: 0                 // 0=空闲, 1=投喂中
val alarmFlags = frame18?.var1b2 ?: 0                   // bit0/1/2 = 余量/卡堵/断电

// frame19: 投喂次数、时间戳、当前方案、计划开关
val feedCountToday = frame19?.var4b1 ?: 0
val systemSec = frame19?.var4b2 ?: 0
val currentPlanId = frame19?.var1b1 ?: 0
val planEnabled = frame19?.var1b2 ?: 0

// frame1A: 计划广播
val planFreqBits = frame1A?.var4b1 ?: 0
val planFreqSec = Float.fromBits(planFreqBits)
val planWeightBits = frame1A?.var4b2 ?: 0
val planWeightG = Float.fromBits(planWeightBits)
val planSyncId = frame1A?.var1b1 ?: 0
val planTotal = frame1A?.var1b2 ?: 0
```

> 说明：`var4b1` / `var4b2` 在 `BleProtocol` 中以 `Int`（32 位整型 bit pattern）形式存储，float 字段通过 `Float.fromBits()` 还原；uint32 字段直接当 `Long`/`Int` 使用即可。**Claude Code 必须先查阅 `BleProtocol.kt` 确认实际字段类型**，本示例仅展示通用模式。如 `var4b1` 在该工程中已经是 `Float` 类型，则直接读取无需 `fromBits`。

### 数据发送代码示例

```kotlin
// 维持一个全局 cmdSeq，每次发送命令递增
var cmdSeq by remember { mutableIntStateOf(0) }

// 工具函数：发送命令帧
fun sendCommand(
    cmdCode: Int,
    planId: Int = 0,
    freqSec: Float = 0f,
    weightG: Float = 0f
) {
    cmdSeq = (cmdSeq + 1) and 0xFF

    // 0x21 帧：参数 + 命令码 + 方案ID
    val frame21 = BleProtocol.buildTxFrame(
        cmd = 0x21,
        var4_1 = freqSec.toRawBits(),
        var4_2 = weightG.toRawBits(),
        var1_1 = cmdCode,
        var1_2 = planId
    )
    BleConnectionManager.writeCharacteristic(
        serviceUuid = UserConfig.esp32_service_1_uuid,
        characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
        value = frame21
    )
    BleConnectionManager.recordOutgoingMessage(
        characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
        value = frame21
    )

    // 0x22 帧：阈值（仅设置阈值时有意义） + 序列号
    val frame22 = BleProtocol.buildTxFrame(
        cmd = 0x22,
        var4_1 = 0,
        var4_2 = 0,
        var1_1 = cmdSeq,
        var1_2 = 0
    )
    BleConnectionManager.writeCharacteristic(
        serviceUuid = UserConfig.esp32_service_1_uuid,
        characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
        value = frame22
    )
    BleConnectionManager.recordOutgoingMessage(
        characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
        value = frame22
    )
}

// 设置报警阈值的特殊版本
fun sendThreshold(thresholdG: Float) {
    cmdSeq = (cmdSeq + 1) and 0xFF
    val frame21 = BleProtocol.buildTxFrame(
        cmd = 0x21,
        var4_1 = 0,
        var4_2 = 0,
        var1_1 = 8,    // CMD_THRESHOLD_SET
        var1_2 = 0
    )
    BleConnectionManager.writeCharacteristic(...)
    val frame22 = BleProtocol.buildTxFrame(
        cmd = 0x22,
        var4_1 = thresholdG.toRawBits(),
        var4_2 = 0,
        var1_1 = cmdSeq,
        var1_2 = 0
    )
    BleConnectionManager.writeCharacteristic(...)
}
```

> **重要**：每条命令必须**同时**发送 0x21 + 0x22 两帧（先 0x21 后 0x22），STM32 端通过 0x22 帧的 cmdSeq 触发命令处理。如果只发 0x21，STM32 不会执行该命令。
>
> Claude Code 实现时如发现 `BleProtocol.buildTxFrame` 的 `var4_1`/`var4_2` 参数类型与上述示例不一致（如本工程中是 `Int` 而非 `Long`），按工程实际类型转换即可。`Float.toRawBits()` 返回 `Int`，可直接传入。

---

## 四、UI 界面设计

### 强制要求：标题区域

`HomeScreen()` 顶部必须包含一个标题区域：

- **项目名称**：宠物智能饲喂系统设计与实现（大字体居中显示）
- **项目作者**：韦孙麟（小字体，显示在项目名称下方）

实现方式：在 `userConfig.kt` 中定义两个常量（如该文件存在）：

```kotlin
const val PROJECT_TITLE = "宠物智能饲喂系统设计与实现"
const val PROJECT_AUTHOR = "韦孙麟"
```

或在 `MainActivity.kt` 顶部直接定义。

### UI 设计原则（必须遵守）

1. **STM32 → APP 的数据（接收）= 文本显示**
   - 用 `HomeSection` 分区
   - 用 `Text` 或 `ProtoFieldRow` 显示
   - 格式示例：`当前余量：38.5 g`、`投喂状态：投喂中`

2. **APP → STM32 的控制（发送）= 按钮**
   - 所有控制操作一律用 `Button` 实现
   - 按钮上写明操作描述
   - 需要数值的功能用预设档位按钮代替（如阈值用 `[10g] [20g] [30g] [50g] [100g]`）

3. **禁止使用**：Slider、SeekBar、自定义拖动控件

### 页面结构（顺序自上而下）

#### 区域 0：标题区域

- 大字体显示 `PROJECT_TITLE`（宠物智能饲喂系统设计与实现）
- 小字体显示 `PROJECT_AUTHOR`（韦孙麟）

#### 区域 1：系统状态（HomeSection: "系统状态"）

显示内容（来自 0x18 + 0x19 帧）：

| 显示项 | 数据来源 | 格式 |
|---|---|---|
| 当前食槽余量 | TX[4] currentWeightG | 保留 1 位小数 + " g"，如 `38.5 g` |
| 投喂状态 | TX[6] feedingState | 0→`空闲`，1→`投喂中` |
| 当前执行方案 | TX[10] currentPlanId | 0→`无`，否则→`方案 N` |
| 计划执行开关 | TX[11] planEnabled | 0→`已禁用`，1→`已启用` |
| 当日投喂次数 | TX[8] feedCountToday | 整数 + " 次" |

#### 区域 2：异常报警（HomeSection: "异常报警"）

显示内容（来自 0x18 帧 TX[7] alarmFlags 的位）：

- bit0 = 1 → 显示红色文字 `⚠️ 食槽余量不足`，否则显示绿色 `✓ 余量正常`
- bit1 = 1 → 显示红色 `⚠️ 电机卡堵故障`，否则显示绿色 `✓ 电机正常`
- bit2 = 1 → 显示红色 `⚠️ 外部断电`，否则显示绿色 `✓ 供电正常`

3 项分行显示。报警时配色用 `MaterialTheme.colorScheme.error`，正常时用 `MaterialTheme.colorScheme.primary` 或绿色。

#### 区域 3：计划执行控制（HomeSection: "计划执行控制"）

显示当前 `planEnabled` 状态，并提供两个按钮：

- `[启用计划执行]` → 调用 `sendCommand(cmdCode = 5)`
- `[禁用计划执行]` → 调用 `sendCommand(cmdCode = 6)`

按钮根据当前 `planEnabled` 状态启用/禁用：已启用时灰显「启用」按钮，已禁用时灰显「禁用」按钮。

#### 区域 4：手动控制（HomeSection: "手动控制（计划禁用时有效）"）

仅在 `planEnabled == 0` 时启用以下按钮：

- `[手动开启食槽]` → `sendCommand(cmdCode = 3)`
- `[手动关闭食槽]` → `sendCommand(cmdCode = 4)`

`planEnabled == 1` 时按钮灰显并提示"请先禁用计划执行"。

#### 区域 5：当前方案选择（HomeSection: "当前方案选择"）

显示文字：`当前方案：方案 N`（来自 TX[10]）

提供 5 个按钮：`[方案1] [方案2] [方案3] [方案4] [方案5]`，点击对应按钮调用 `sendCommand(cmdCode = 7, planId = N)`。

当前选中的方案按钮高亮显示（如使用 `Button` vs `OutlinedButton`，或不同颜色）。

#### 区域 6：报警阈值设置（HomeSection: "报警阈值设置"）

显示当前阈值：`当前阈值：20.0 g`（来自 TX[5]）

提供预设档位按钮：`[10g] [20g] [30g] [50g] [100g] [200g]`，点击调用 `sendThreshold(thresholdG = X)`。

当前阈值对应的按钮高亮显示。

#### 区域 7：投喂计划管理（HomeSection: "投喂计划管理"）

**计划列表显示**：

APP 端维护一个本地计划缓存，长度 5：

```kotlin
data class PlanSlot(
    val planId: Int,         // 0 表示空槽位
    val frequencySec: Float,
    val weightG: Float
)
val planCache = remember { mutableStateListOf(
    PlanSlot(0, 0f, 0f), PlanSlot(0, 0f, 0f), PlanSlot(0, 0f, 0f),
    PlanSlot(0, 0f, 0f), PlanSlot(0, 0f, 0f)
)}
```

每次 0x1A 帧的 `planSyncId` 变化时（用 `LaunchedEffect` 监听），将广播到的方案写入对应索引（`planSyncId - 1`）。`planSyncId == 0` 表示该槽位空。

显示 5 行（对应 5 个槽位）：

- 已使用：`方案 N：每 30s 投喂 50g  [删除] [修改]`
- 空槽位：`方案 N：(未配置)  [新建]`

**新建/修改对话框**：

点击 `[新建]` 或 `[修改]` 弹出 `AlertDialog`，包含：

- 频率档位按钮：`[10s] [30s] [60s] [300s] [600s] [1800s] [3600s]`
- 重量档位按钮：`[10g] [20g] [30g] [50g] [100g]`
- `[确认]` 按钮 → `sendCommand(cmdCode = 1, planId = N, freqSec = ?, weightG = ?)` 并关闭对话框
- `[取消]` 按钮 → 仅关闭对话框

**删除按钮**：直接调用 `sendCommand(cmdCode = 2, planId = N)`，可加二次确认对话框。

---

## 五、交互逻辑

### 数据刷新

- `BleProtocol.rxFrames` 是 `mutableStateOf`，Compose 自动重组，无需手动刷新
- 计划列表缓存通过 `LaunchedEffect(planSyncId, planFreqSec, planWeightG)` 监听变化更新

### 命令序列号管理

- 在 `HomeScreen()` 顶层维护一个 `cmdSeq` 状态变量，每次 `sendCommand` / `sendThreshold` 调用时递增（`(cmdSeq + 1) and 0xFF`）
- 序列号写入 `0x22` 帧的 `var1_1`

### 按钮状态管理

- "启用计划执行" / "禁用计划执行" 按钮根据 `planEnabled` 互斥灰显
- "手动开启/关闭" 按钮在 `planEnabled == 1` 时整体灰显
- 当前方案、当前阈值的按钮高亮显示

### 异常状态 UI 表现

- 报警区域使用 `MaterialTheme.colorScheme.errorContainer` / `error` 配色
- 投喂状态为"投喂中"时可用强调色或图标提示
- BLE 未连接时（可根据 `BleConnectionManager` 暴露的连接状态，如已实现），所有按钮禁用并显示提示"设备未连接"

### 边界处理

- 若 `BleProtocol.rxFrames[0x18] == null`（未收到任何数据），所有显示项使用占位 `--`
- 计划缓存初始全为空槽位，等待 STM32 广播填充
- 频率档位按钮选中后视觉反馈（如背景色），重量同理

---

## 六、输出物要求

Claude Code 完成开发后必须输出：

### 1. 代码文件

- 修改后的 `MainActivity.kt`（在新增/修改区域用注释 `// === 修改：xxx ===` 和 `// === END ===` 标注）
- 如修改了 `userConfig.kt`，输出修改后的版本并标注新增的 `PROJECT_TITLE` 和 `PROJECT_AUTHOR` 常量

### 2. 功能实现文档（`app_implementation_report.md`）

#### (a) 功能实现状态清单

| 序号 | 功能描述 | 状态 | 说明 |
|---|---|---|---|
| 1 | 项目标题区域 | ✅/⚠️/❌ | |
| 2 | 系统状态显示 | ✅/⚠️/❌ | |
| 3 | 异常报警显示 | ✅/⚠️/❌ | |
| 4 | 计划执行启用/禁用 | ✅/⚠️/❌ | |
| 5 | 手动控制舵机 | ✅/⚠️/❌ | |
| 6 | 当前方案选择 | ✅/⚠️/❌ | |
| 7 | 报警阈值设置 | ✅/⚠️/❌ | |
| 8 | 计划列表显示 | ✅/⚠️/❌ | |
| 9 | 计划新增/修改对话框 | ✅/⚠️/❌ | |
| 10 | 计划删除 | ✅/⚠️/❌ | |
| 11 | 命令序列号管理 | ✅/⚠️/❌ | |

状态：✅ 已实现，⚠️ 部分实现（说明缺什么），❌ 未实现（说明原因）

#### (b) 已实现功能的测试步骤

对每个功能给出测试方法和预期结果，例如：

> **功能 7（报警阈值设置）测试**
> 1. 进入主页面，确认显示"当前阈值：20.0 g"（默认）
> 2. 点击 `[50g]` 按钮
> 3. 1~2 秒后页面应显示"当前阈值：50.0 g"，且 `[50g]` 按钮高亮
> 4. 在 STM32 端用调试器观察 `user1TaskInfo.alarmThresholdG` 应等于 50.0

> **功能 8 + 9（计划新增）测试**
> 1. 主页面计划区域显示 5 行，初始全部为"(未配置)"
> 2. 点击方案1的 `[新建]` 按钮，弹出对话框
> 3. 选择频率 `30s`、重量 `50g`，点击 `[确认]`
> 4. 5 秒内（一轮广播周期）该行应更新为"方案 1：每 30s 投喂 50g"
> 5. 重启 STM32 设备，重连 BLE 后该方案应仍存在（断电记忆验证）

---

## 七、验证清单（开发完成后必须自检）

- [ ] `BleProtocol.kt`、`BleConnectionManager.kt`、`BleAutoConnector.kt` 等通信底层未做任何改动
- [ ] 没有从 `BleProtocol.rxFrames[0x17]` 读取任何 STM32 业务数据（0x17 是 ESP32 专属帧）
- [ ] 所有读取均从 0x18、0x19、0x1A 帧获取
- [ ] 所有发送均通过 0x21 + 0x22 两帧组合
- [ ] 每次发送命令时 cmdSeq 递增并写入 0x22 的 `var1_1`
- [ ] 标题区域显示项目名称（宠物智能饲喂系统设计与实现）和作者（韦孙麟）
- [ ] 没有使用 Slider、SeekBar 或自定义拖动控件
- [ ] 所有控制均通过 Button 触发
- [ ] 所有显示项均为 Text / ProtoFieldRow（接收侧不出现可编辑控件）
- [ ] UI 使用 Jetpack Compose，复用 `HomeSection` / `ProtoFieldRow`
- [ ] 异常状态有视觉反馈（颜色/图标区分）
- [ ] 实现文档（`app_implementation_report.md`）已生成并填写完整
- [ ] 编译通过，无新增警告
