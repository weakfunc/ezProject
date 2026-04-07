# APP 软件需求文档 — 超声波液位高度测量系统

---

## 一、项目概述

本 APP 配合 STM32 液位测量装置使用，通过 BLE 实时接收测量数据并显示，同时提供报警阈值设置功能。

**APP 主要功能**：
1. 显示当前液位高度（mm）
2. 显示液位变化量（mm，带正负号）
3. 显示模块距液面原始距离（mm）
4. 显示系统状态（校准中 / 正常工作）
5. 显示当前生效的报警上限回显（mm）
6. 提供液位高报警阈值输入框 + 设置按钮（发送高限至 STM32）
7. 提供液位低报警阈值输入框 + 设置按钮（发送低限至 STM32）

---

## 二、开发边界

**只修改** `MainActivity.kt` 中的 `HomeScreen()` 函数，实现全部 UI 展示和控制交互。

**不可修改**：`BleProtocol.kt`、`BleConnectionManager.kt`、`BleAutoConnector.kt`、`AppBottomNavigation.kt`、`userConfig.kt`、`ui/theme/` 等通信底层和框架文件。

**UI 框架**：Jetpack Compose + Material Design 3，可复用 `HomeSection()` 和 `ProtoFieldRow()` 组件。

---

## 三、通信变量映射表

### APP 接收（显示用）：STM32 → APP

STM32 端从 `remoteVar_TX[4]` 开始使用（`remoteVar_TX[0]~[3]` 未使用），对应 APP 端从 CMD `0x18` 开始接收。

| 数据含义 | APP RX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_TX 索引 |
|---------|-----------|------|---------|---------------------------|
| 当前液位高度（mm） | 0x18 | var4b1 | Float（`Float.fromBits()`转换） | [4] |
| 液位变化量（mm，正=上升/负=下降） | 0x18 | var4b2 | Float（`Float.fromBits()`转换） | [5] |
| 系统状态（0=校准中，1=正常工作） | 0x18 | var1b1 | Int（低8位有效） | [6] |
| 保留 | 0x18 | var1b2 | — | [7] |
| 模块距液面原始距离（mm） | 0x19 | var4b1 | Float（`Float.fromBits()`转换） | [8] |
| 当前报警上限回显（mm） | 0x19 | var4b2 | Float（`Float.fromBits()`转换） | [9] |
| 保留 | 0x19 | var1b1 | — | [10] |
| 保留 | 0x19 | var1b2 | — | [11] |

> **注意**：`remoteVar_TX[0]~[3]`（对应 CMD `0x17`）STM32 端未写入，APP 端**不读取** `0x17` 的任何槽位。

**读取代码示例**：

```kotlin
// 读取帧1（0x18）：液位高度、变化量、系统状态
val frame1 = BleProtocol.rxFrames[0x18]
val liquidLevel_mm   = Float.fromBits(frame1?.var4b1 ?: 0)   // 液位高度
val liquidDelta_mm   = Float.fromBits(frame1?.var4b2 ?: 0)   // 变化量（带正负）
val systemStatus     = frame1?.var1b1 ?: 0                   // 0=校准中，1=正常

// 读取帧2（0x19）：原始距离、报警上限回显
val frame2 = BleProtocol.rxFrames[0x19]
val distToLiquid_mm  = Float.fromBits(frame2?.var4b1 ?: 0)   // 距液面距离
val alarmHighEcho_mm = Float.fromBits(frame2?.var4b2 ?: 0)   // 高限回显
```

---

### APP 发送（控制用）：APP → STM32

STM32 端使用 `remoteVar_RX[0]`、`remoteVar_RX[1]` 接收阈值，对应 APP 端 CMD `0x21`。

| 操作含义 | APP TX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_RX 索引 |
|---------|-----------|------|---------|---------------------------|
| 设置液位高报警阈值（mm） | 0x21 | var4_1 | Float（`.toBits()`转换） | [0] |
| 设置液位低报警阈值（mm） | 0x21 | var4_2 | Float（`.toBits()`转换） | [1] |
| 保留（填0） | 0x21 | var1_1 | — | [2] |
| 保留（填0） | 0x21 | var1_2 | — | [3] |

**发送代码示例**：

```kotlin
fun sendAlarmThreshold(highMm: Float, lowMm: Float) {
    val frame = BleProtocol.buildTxFrame(
        cmd    = 0x21,
        var4_1 = highMm.toBits(),   // 高限 float → int bits
        var4_2 = lowMm.toBits(),    // 低限 float → int bits
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

`HomeScreen()` 顶部必须包含标题区域：

```
项目名称（大字体，居中）：液位高度测量系统
项目作者（小字体，居中，名称下方）：（留空）
```

### 功能区域划分

使用 `HomeSection` 组件划分以下三个区域：

---

#### 区域一：系统状态

```kotlin
HomeSection(title = "系统状态") {
    val statusText  = if (systemStatus == 0) "正在校准..." else "正常工作"
    val statusColor = if (systemStatus == 0) Color(0xFFFF9800) else Color(0xFF4CAF50)
    Text(
        text  = statusText,
        color = statusColor,
        style = MaterialTheme.typography.titleMedium
    )
}
```

---

#### 区域二：实时测量数据

校准中（`systemStatus == 0`）时所有数值显示 `"---"`；正常工作时显示实际数值。

| 字段 | 显示格式 |
|------|---------|
| 液位高度 | `"%.0f mm".format(liquidLevel_mm)` |
| 液位变化量 | `"%+.0f mm".format(liquidDelta_mm)`（强制带正负号） |
| 距液面距离 | `"%.0f mm".format(distToLiquid_mm)` |
| 当前高限 | `alarmHighEcho_mm > 0f` → `"%.0f mm".format(alarmHighEcho_mm)`，否则 `"未设置"` |

```kotlin
HomeSection(title = "实时测量数据") {
    val isCalib = systemStatus == 0
    ProtoFieldRow(
        label1 = "液位高度",
        value1 = if (isCalib) "---" else "%.0f mm".format(liquidLevel_mm),
        label2 = "液位变化量",
        value2 = if (isCalib) "---" else "%+.0f mm".format(liquidDelta_mm)
    )
    ProtoFieldRow(
        label1 = "距液面距离",
        value1 = if (isCalib) "---" else "%.0f mm".format(distToLiquid_mm),
        label2 = "当前高限",
        value2 = if (isCalib) "---"
                 else if (alarmHighEcho_mm > 0f) "%.0f mm".format(alarmHighEcho_mm)
                 else "未设置"
    )
}
```

---

#### 区域三：报警阈值设置

两个数值输入框 + 一个设置按钮。

**输入校验规则**（按顺序判断，任一不通过则 Toast 提示并不发送）：

| 校验条件 | Toast 提示 |
|---------|-----------|
| 任一输入框为空或非数字 | `"请输入有效数值"` |
| 高限或低限 ≤ 0 | `"阈值须大于0"` |
| 高限 > 1000 或低限 > 1000 | `"阈值超出范围（最大1000mm）"` |
| 高限 ≤ 低限 | `"高限须大于低限"` |
| 校验全部通过 | 调用 `sendAlarmThreshold(high, low)` |

```kotlin
HomeSection(title = "报警阈值设置") {
    var highInput by remember { mutableStateOf("") }
    var lowInput  by remember { mutableStateOf("") }
    val context = LocalContext.current

    OutlinedTextField(
        value         = highInput,
        onValueChange = { highInput = it },
        label         = { Text("高报警阈值 (mm)") },
        placeholder   = { Text("如：800") },
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        modifier      = Modifier.fillMaxWidth()
    )
    Spacer(modifier = Modifier.height(8.dp))
    OutlinedTextField(
        value         = lowInput,
        onValueChange = { lowInput = it },
        label         = { Text("低报警阈值 (mm)") },
        placeholder   = { Text("如：200") },
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        modifier      = Modifier.fillMaxWidth()
    )
    Spacer(modifier = Modifier.height(12.dp))
    Button(
        onClick = {
            val high = highInput.toFloatOrNull()
            val low  = lowInput.toFloatOrNull()
            when {
                high == null || low == null  -> Toast.makeText(context, "请输入有效数值", Toast.LENGTH_SHORT).show()
                high <= 0f || low <= 0f      -> Toast.makeText(context, "阈值须大于0", Toast.LENGTH_SHORT).show()
                high > 1000f || low > 1000f  -> Toast.makeText(context, "阈值超出范围（最大1000mm）", Toast.LENGTH_SHORT).show()
                high <= low                  -> Toast.makeText(context, "高限须大于低限", Toast.LENGTH_SHORT).show()
                else                         -> sendAlarmThreshold(high, low)
            }
        },
        modifier = Modifier.fillMaxWidth()
    ) {
        Text("设置阈值")
    }
}
```

---

## 五、交互逻辑

**数据刷新**：`BleProtocol.rxFrames` 为 `mutableStateOf`，Compose UI 读取时自动触发重组，无需手动刷新。

**校准期间**：实时数据区所有数值显示 `"---"`；阈值设置按钮仍可操作，STM32 标定完成后立即生效。

**设置确认**：发送成功后观察"当前高限"字段——若更新为用户输入的高限值，说明设置已生效（STM32 通过 TX[9] 回显）。

---

## 六、输出物要求

### 1. 代码文件

修改后的 `MainActivity.kt`，在修改区域加注释标注：

```kotlin
// ===== [修改开始] 液位测量系统 UI =====
// ...
// ===== [修改结束] =====
```

### 2. 功能实现文档（`app_implementation_report.md`）

#### (a) 功能实现状态清单

| 序号 | 功能描述 | 状态 | 说明 |
|------|---------|------|------|
| 1 | 液位高度实时显示 | ✅/⚠️/❌ | |
| 2 | 液位变化量显示（带符号） | ✅/⚠️/❌ | |
| 3 | 距液面原始距离显示 | ✅/⚠️/❌ | |
| 4 | 系统状态显示（校准中/正常） | ✅/⚠️/❌ | |
| 5 | 报警上限回显 | ✅/⚠️/❌ | |
| 6 | 高限阈值输入框 + 设置按钮 | ✅/⚠️/❌ | |
| 7 | 低限阈值输入框 + 设置按钮 | ✅/⚠️/❌ | |
| 8 | 输入校验与 Toast 提示 | ✅/⚠️/❌ | |

#### (b) 测试步骤

| 功能 | 测试方法 | 预期结果 |
|------|---------|---------|
| 液位高度显示 | BLE 连接后，对比 OLED 与 APP 显示值 | 两端数值一致，误差 ≤ 1 mm |
| 状态切换 | 设备上电后观察 APP 状态区 | 约500ms内从"正在校准..."变为"正常工作" |
| 发送高限阈值 | 输入高限800、低限200，点设置 | "当前高限"字段更新为 800 mm |
| 输入校验-空值 | 清空输入框点设置 | Toast"请输入有效数值" |
| 输入校验-高≤低 | 高限200低限800，点设置 | Toast"高限须大于低限" |
| 输入校验-超范围 | 输入1500，点设置 | Toast"阈值超出范围（最大1000mm）" |
| 校准中状态 | 设备刚上电，观察数据区 | 所有数值显示"---" |

---

*文档版本：v1.1（更新通信布局：TX 从 remoteVar_TX[4] 开始，对应 APP RX CMD 0x18）*
