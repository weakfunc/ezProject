# APP 软件需求文档
# 两轮电动车智能防盗与定位系统

> 本文档供 APP 项目目录下的 Claude Code 执行开发，拿到本文档 + 项目源码即可完成开发，无需其他文档。

---

## 一、项目概述

APP 需要实现以下界面和交互功能：

1. **顶部标题区**：显示项目名称和作者信息
2. **GPS 实时位置**：接收 STM32 发来的经纬度数据，在地图上显示实时位置（调用系统地图 Intent 或嵌入地图 SDK；若复杂度过高，简化为显示坐标数值 + 一个"在地图中查看"按钮，打开系统地图应用）
3. **历史运动轨迹回放**：APP 端本地存储收到的 GPS 坐标序列（带帧序号），提供轨迹回放界面（滚动列表或折线图展示历史坐标）
4. **防盗状态显示**：显示当前防盗开关状态、报警状态
5. **防盗远程控制**：按钮开启/关闭防盗功能，按钮消除报警
6. **系统状态显示**：显示系统当前状态（正常/休眠/报警中）

---

## 二、开发边界

Claude Code 必须遵守以下开发边界：

- **修改**：`MainActivity.kt` 中的 `HomeScreen()` 函数；`userConfig.kt` 中的项目名称和项目作者信息
- **不修改**：`BleProtocol.kt`、`BleConnectionManager.kt`、`BleAutoConnector.kt` 等通信底层文件
- UI 使用 **Jetpack Compose + Material Design 3**
- 可复用现有 `HomeSection()` 和 `ProtoFieldRow()` 组件
- APP 端所有业务逻辑（GPS 存储、轨迹回放）用 Kotlin 在 `MainActivity.kt` 内实现，使用 `remember`/`mutableStateOf` 管理状态，**禁止使用 localStorage/数据库**（内存存储即可，方案简单优先）

---

## 三、通信变量映射表

### ⚠️ 重要说明
- **0x17 帧由 ESP32 专属发送，不来自 STM32**，APP 不得从 0x17 帧读取 STM32 数据
- **STM32 数据从 0x18 帧开始**

### APP 接收（显示用）

| 数据含义 | APP RX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_TX 索引 |
|---------|-----------|------|---------|---------------------------|
| GPS 纬度 | 0x18 | var4b1 | Float（直接作为 float 读取）| TX[4] |
| GPS 经度 | 0x18 | var4b2 | Float（直接作为 float 读取）| TX[5] |
| GPS 定位状态（0=无效，1=有效） | 0x18 | var1b1 | Int（取低8位）| TX[6] |
| 防盗报警标志（0=正常，1=报警） | 0x18 | var1b2 | Int（取低8位）| TX[7] |
| GPS 帧序号（uint32，用于轨迹排序）| 0x19 | var4b1 | Int | TX[8] |
| （暂留）| 0x19 | var4b2 | — | TX[9] |
| 防盗开关当前状态（0=关闭，1=开启）| 0x19 | var1b1 | Int（取低8位）| TX[10] |
| 系统状态（0=正常，1=休眠，2=报警中）| 0x19 | var1b2 | Int（取低8位）| TX[11] |

### APP 发送（控制用）

| 操作含义 | APP TX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_RX 索引 |
|---------|-----------|------|---------|---------------------------|
| 防盗开关指令（0=关闭，1=开启） | 0x21 | var1_1 | Int（1字节）| RX[2] |
| 报警消除指令（发送 1 触发）| 0x21 | var1_2 | Int（1字节）| RX[3] |

### 读取代码示例

```kotlin
// 读取 STM32 GPS 和基础状态数据（0x18 帧）
val frame18 = BleProtocol.rxFrames[0x18]
val gpsLatitude  = frame18?.var4b1?.let { java.lang.Float.intBitsToFloat(it) } ?: 0f  // 纬度
val gpsLongitude = frame18?.var4b2?.let { java.lang.Float.intBitsToFloat(it) } ?: 0f  // 经度
val gpsValid     = frame18?.var1b1 ?: 0   // GPS 状态（0=无效，1=有效）
val alarmFlag    = frame18?.var1b2 ?: 0   // 报警标志（0=正常，1=报警）

// 读取防盗状态和系统状态（0x19 帧）
val frame19 = BleProtocol.rxFrames[0x19]
val gpsFrameSeq       = frame19?.var4b1 ?: 0   // GPS 帧序号
val antitheftEnabled  = frame19?.var1b1 ?: 0   // 防盗开关状态
val systemStatus      = frame19?.var1b2 ?: 0   // 系统状态
```

> **注意**：`var4b1` / `var4b2` 存储的是 float 的 bit 表示（int 类型），需要用 `java.lang.Float.intBitsToFloat()` 转换为 float。

### 发送代码示例

```kotlin
// 发送防盗开关指令（开启：antitheftOn=1，关闭：antitheftOn=0）
fun sendAntitheftCmd(antitheftOn: Int, clearAlarm: Int = 0) {
    val frame = BleProtocol.buildTxFrame(
        cmd    = 0x21,
        var4_1 = 0,          // RX[0]，暂留
        var4_2 = 0,          // RX[1]，暂留
        var1_1 = antitheftOn,  // RX[2]，防盗开关指令
        var1_2 = clearAlarm    // RX[3]，报警消除指令
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

### 4.0 userConfig.kt 修改

在 `userConfig.kt` 中更新以下字段：
```kotlin
val projectName   = "两轮电动车智能防盗与定位系统设计"
val projectAuthor = "2200810602陈敏敏"
```

### 4.1 顶部标题区（强制要求）

`HomeScreen()` 顶部必须包含标题区域：

```kotlin
// 项目名称（大字体居中）
Text(
    text     = "两轮电动车智能防盗与定位系统设计",
    style    = MaterialTheme.typography.titleLarge,
    textAlign = TextAlign.Center
)
// 项目作者（小字体，居中）
Text(
    text  = "2200810602陈敏敏",
    style = MaterialTheme.typography.bodySmall
)
```

### 4.2 功能区划分

标题区之后，用 `HomeSection` 划分以下功能区（从上到下）：

---

#### 区域 1：GPS 实时位置

**显示内容：**
- GPS 状态：`有效` / `无效`（根据 `gpsValid` 判断）
- 纬度：显示 float 值，保留 6 位小数，格式 `23.456789°N`
- 经度：显示 float 值，保留 6 位小数，格式 `113.456789°E`
- 帧序号：`#序号`（供调试观察是否在更新）

**控制按钮：**
- `[在地图中查看]` — 点击后调用 Android 地图 Intent，打开系统地图应用显示当前坐标：
  ```kotlin
  val uri = Uri.parse("geo:$gpsLatitude,$gpsLongitude?q=$gpsLatitude,$gpsLongitude")
  val intent = Intent(Intent.ACTION_VIEW, uri)
  context.startActivity(intent)
  ```
  若 GPS 无效，按钮置灰或 Toast 提示"GPS 未定位"。

---

#### 区域 2：历史轨迹

**功能说明：**
APP 端在内存中维护一个 GPS 坐标列表（`remember { mutableStateListOf<GpsPoint>() }`），每当收到新的 GPS 帧序号（与上次不同）且 GPS 有效时，追加一条记录。列表最多保存 500 条（超出时删除最旧的）。

```kotlin
data class GpsPoint(val seq: Int, val lat: Float, val lon: Float)
```

**显示内容：**
- 已记录轨迹点数：`已记录 XX 个点`
- 最新一条坐标（小字体）：`最新: 23.456789, 113.456789`

**控制按钮：**
- `[查看轨迹列表]` — 点击后展开/折叠一个懒加载列表（`LazyColumn`），显示最近 20 条轨迹记录，格式：`#序号  纬度, 经度`
- `[清空轨迹]` — 清空内存中的轨迹列表

---

#### 区域 3：防盗控制

**显示内容：**
- 防盗状态：`已开启` / `已关闭`（根据 `antitheftEnabled` 判断）
- 报警状态：`正常` / `⚠️ 报警中`（根据 `alarmFlag` 判断，报警时用红色高亮文字）

**控制按钮：**
- `[开启防盗]` — 点击发送防盗开启指令（`antitheftOn=1`）；当防盗已开启时按钮文字改为 `[关闭防盗]`，点击发送关闭指令（`antitheftOn=0`）
  - 即：同一个按钮，根据当前 `antitheftEnabled` 状态切换文字和发送值
- `[消除报警]` — 点击发送报警消除指令（`clearAlarm=1`）；当 `alarmFlag==0` 时按钮置灰

---

#### 区域 4：系统状态

**显示内容：**
- 系统状态：根据 `systemStatus` 值显示文字
  - 0 → `正常运行`
  - 1 → `设备休眠中`
  - 2 → `⚠️ 报警中`
  - 其他 → `未知`
- BLE 连接状态：从 BleConnectionManager 读取连接状态（已有 API），显示 `已连接` / `未连接`

---

## 五、交互逻辑

**数据刷新：** `BleProtocol.rxFrames` 是 `mutableStateOf`，Compose UI 读取时自动触发重组，无需手动刷新。

**GPS 轨迹追加逻辑：**
```kotlin
// 在 LaunchedEffect 或 derivedStateOf 中监听 gpsFrameSeq 变化
val prevSeq = remember { mutableStateOf(-1) }
if (gpsValid == 1 && gpsFrameSeq != prevSeq.value) {
    if (gpsTrackList.size >= 500) gpsTrackList.removeFirst()
    gpsTrackList.add(GpsPoint(seq = gpsFrameSeq, lat = gpsLatitude, lon = gpsLongitude))
    prevSeq.value = gpsFrameSeq
}
```

**防盗按钮状态切换：**
```kotlin
Button(onClick = {
    val cmd = if (antitheftEnabled == 1) 0 else 1
    sendAntitheftCmd(antitheftOn = cmd)
}) {
    Text(if (antitheftEnabled == 1) "关闭防盗" else "开启防盗")
}
```

**消除报警按钮置灰：**
```kotlin
Button(
    onClick  = { sendAntitheftCmd(antitheftOn = antitheftEnabled, clearAlarm = 1) },
    enabled  = alarmFlag == 1
) { Text("消除报警") }
```

**报警状态颜色：**
```kotlin
Text(
    text  = if (alarmFlag == 1) "⚠️ 报警中" else "正常",
    color = if (alarmFlag == 1) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface
)
```

**所有控制通过按钮点击触发**，点击后调用 `sendAntitheftCmd()` 发送对应数据。

---

## 六、输出物要求

Claude Code 完成开发后，必须输出：

### 1. 代码文件

- 修改后的 `MainActivity.kt`，在修改区域前后用注释标注：
  ```kotlin
  // ===== [MODIFIED BEGIN] HomeScreen - 防盗定位系统 UI =====
  // ...
  // ===== [MODIFIED END] =====
  ```

### 2. 功能实现文档（app_implementation_report.md）

#### （a）功能实现状态清单

| 序号 | 功能描述 | 状态 | 说明 |
|------|---------|------|------|
| 1 | 顶部标题区（项目名+作者）| ✅/⚠️/❌ | |
| 2 | GPS 实时坐标显示 | ✅/⚠️/❌ | |
| 3 | 在地图中查看按钮 | ✅/⚠️/❌ | |
| 4 | 历史轨迹记录（内存存储，500条）| ✅/⚠️/❌ | |
| 5 | 历史轨迹列表展示 | ✅/⚠️/❌ | |
| 6 | 防盗开关远程控制 | ✅/⚠️/❌ | |
| 7 | 报警状态显示与消除 | ✅/⚠️/❌ | |
| 8 | 系统状态显示 | ✅/⚠️/❌ | |

#### （b）已实现功能的测试步骤

- **GPS 数据显示测试**：BLE 连接设备，观察纬度/经度是否每秒更新，帧序号是否递增
- **地图跳转测试**：GPS 有效时点击"在地图中查看"，系统应弹出地图应用并定位到对应坐标
- **轨迹记录测试**：GPS 有效后等待若干秒，检查"已记录 XX 个点"数量是否增加，轨迹列表中是否有记录
- **防盗控制测试**：点击"开启防盗"，STM32 OLED 或 APP 防盗状态应变为"已开启"；再点击"关闭防盗"，状态应恢复
- **报警消除测试**：触发报警后，APP 显示"⚠️ 报警中"，点击"消除报警"，状态应恢复为"正常"
