# APP 软件需求文档 — QR 码自动分拣系统

---

## 一、项目概述

本 APP 配合 STM32 QR 码自动分拣装置使用，通过 BLE 实时接收设备数据并显示。

**APP 主要功能**：
1. 显示系统运行时间（s）
2. 显示系统使能状态（使能 / 禁用）
3. 显示当前分拣状态（等待/延时/移动/分拣/回原点/超温保护/过流保护）
4. 显示最近识别的 QR 码目标编号（T01~T10 / 无数据）
5. 显示丝杆当前位置（cm，相对原点）
6. 显示 QR 码累计接收计数

APP **只接收数据**，无需向 STM32 发送任何数据。

---

## 二、开发边界

**只修改** `MainActivity.kt` 中的 `HomeScreen()` 函数，实现全部 UI 展示。

**不可修改**：`BleProtocol.kt`、`BleConnectionManager.kt`、`BleAutoConnector.kt`、`AppBottomNavigation.kt`、`ui/theme/` 等通信底层和框架文件。

**UI 框架**：Jetpack Compose + Material Design 3，可复用 `HomeSection()` 和 `ProtoFieldRow()` 组件。

---

## 三、通信变量映射表

### APP 接收（显示用）：STM32 → APP

STM32 从 `remoteVar_TX[0]` 开始写入，对应 APP 端从 CMD `0x17` 开始接收。

#### 帧一（APP RX CMD `0x17`，对应 STM32 CMD `0x09`）

| 数据含义 | APP RX CMD | 槽位 | 数据类型 | STM32 remoteVar_TX 索引 |
|---------|-----------|------|---------|------------------------|
| 丝杆当前位置（cm，相对原点） | 0x17 | var4b1 | Float（`Float.fromBits()` 转换） | [0] |
| QR 码累计接收计数 | 0x17 | var4b2 | Int（无符号32位） | [1] |
| 分拣状态编码（0~6，见下表） | 0x17 | var1b1 | Int（低8位有效） | [2] |
| 最近 QR 码原始值（0xFF = 无数据） | 0x17 | var1b2 | Int（低8位有效） | [3] |

#### 帧二（APP RX CMD `0x18`，对应 STM32 CMD `0x0A`）

| 数据含义 | APP RX CMD | 槽位 | 数据类型 | STM32 remoteVar_TX 索引 |
|---------|-----------|------|---------|------------------------|
| 系统计时原始计数（单位 10ms） | 0x18 | var4b1 | Int（无符号32位） | [4] |
| 保留 | 0x18 | var4b2 | — | [5] |
| 系统使能状态（0=禁用，1=使能） | 0x18 | var1b1 | Int（低8位有效） | [6] |
| 保留 | 0x18 | var1b2 | — | [7] |

#### 分拣状态编码说明（var1b1 of CMD 0x17）

| 编码值 | 含义 |
|-------|------|
| 0 | WAIT — 等待扫描 QR 码 |
| 1 | DLY — 识别后 2s 延时 |
| 2 | MOVE — 丝杆移动至目标地点 |
| 3 | SORT — 舵机执行分拣动作 |
| 4 | HOME — 丝杆返回原点 |
| 5 | OTMP — 超温保护（电机锁定） |
| 6 | OCUR — 过流保护（电机锁定） |

#### 最近 QR 码原始值解析说明（var1b2 of CMD 0x17）

| 值范围 | 含义 |
|-------|------|
| 0x00~0x09 | 目标地点 T01~T10（二进制编码） |
| 0x30~0x39 | 目标地点 T01~T10（ASCII '0'~'9'） |
| 0xFF | 无有效数据（初始状态） |
| 其他 | 未知目标（UNKN） |

#### 读取代码示例

```kotlin
// 读取帧1（0x17）：丝杆位置、QR计数、状态编码、最近QR值
val frame1 = BleProtocol.rxFrames[0x17]
val screwPos_cm  = Float.fromBits(frame1?.var4b1 ?: 0)  // 丝杆位置（cm）
val qrTotalCnt   = frame1?.var4b2 ?: 0                  // QR累计计数
val stateCode    = frame1?.var1b1 ?: 0                  // 状态编码 0~6
val lastQrRaw    = frame1?.var1b2 ?: 0xFF               // 最近QR原始值

// 读取帧2（0x18）：系统计时、系统使能
val frame2 = BleProtocol.rxFrames[0x18]
val sysCnt       = frame2?.var4b1 ?: 0                  // 系统计时原始计数
val sysEnable    = frame2?.var1b1 ?: 0                  // 系统使能 0/1

// 计算显示时间（秒）
val time_s = sysCnt / 100.0f                            // 10ms × count / 1000 × 100
val timeSec  = sysCnt / 100
val time10ms = (sysCnt / 10) % 10
```

---

## 四、UI 界面设计

### 顶部标题区域（强制要求）

`HomeScreen()` 顶部必须包含标题区域：

```
项目名称（大字体，居中）：QR 码自动分拣系统
项目作者（小字体，居中，名称下方）：刘泽霖
```

### 功能区域划分

使用 `HomeSection` 组件划分以下三个区域：

---

#### 区域一：系统状态

显示系统使能状态与运行时间。

```kotlin
HomeSection(title = "系统状态") {
    val enableText  = if (sysEnable != 0) "使能" else "禁用"
    val enableColor = if (sysEnable != 0) Color(0xFF4CAF50) else Color(0xFFF44336)
    ProtoFieldRow(
        label1 = "系统使能",
        value1 = enableText,   // 颜色用 Text 组件单独渲染
        label2 = "运行时间",
        value2 = "${timeSec}.${time10ms} s"
    )
}
```

---

#### 区域二：分拣状态

显示当前状态机状态和最近识别的 QR 目标。

状态文本映射表：

| stateCode | 显示文本 | 颜色 |
|-----------|---------|------|
| 0 | 等待扫码 | 默认 |
| 1 | 识别延时 | 橙色 `0xFFFF9800` |
| 2 | 丝杆移动 | 蓝色 `0xFF2196F3` |
| 3 | 执行分拣 | 蓝色 `0xFF2196F3` |
| 4 | 返回原点 | 橙色 `0xFFFF9800` |
| 5 | 超温保护 | 红色 `0xFFF44336` |
| 6 | 过流保护 | 红色 `0xFFF44336` |

QR 目标文本映射：
- 0x00~0x09 或 0x30~0x39 → `"T%02d".format(idx + 1)`（T01~T10）
- 0xFF → `"无数据"`
- 其他 → `"未知"`

```kotlin
HomeSection(title = "分拣状态") {
    ProtoFieldRow(
        label1 = "状态",
        value1 = stateText,      // 按上表映射
        label2 = "目标地点",
        value2 = qrTargetText    // 按上述规则映射
    )
}
```

---

#### 区域三：实时数据

显示丝杆位置和 QR 累计计数。

```kotlin
HomeSection(title = "实时数据") {
    ProtoFieldRow(
        label1 = "丝杆位置",
        value1 = "%.1f cm".format(screwPos_cm),
        label2 = "QR 计数",
        value2 = qrTotalCnt.toString()
    )
}
```

---

## 五、交互逻辑

**数据刷新**：`BleProtocol.rxFrames` 为 `mutableStateOf`，Compose UI 读取时自动触发重组，无需手动刷新。

**保护状态**：当 stateCode 为 5（超温）或 6（过流）时，状态文本以红色高亮显示，提示用户检查电机。

**无数据状态**：BLE 未连接或初始化阶段，丝杆位置显示 `"0.0 cm"`，QR 目标显示 `"无数据"`，状态显示 `"等待扫码"`。

---

## 六、输出物要求

### 1. 代码文件

修改后的 `MainActivity.kt`，在修改区域加注释标注：

```kotlin
// ===== [修改开始] QR码分拣系统 UI =====
// ...
// ===== [修改结束] =====
```

### 2. 功能实现文档（`app_implementation_report.md`）

#### (a) 功能实现状态清单

| 序号 | 功能描述 | 状态 | 说明 |
|------|---------|------|------|
| 1 | 系统使能状态显示 | ✅/⚠️/❌ | |
| 2 | 运行时间显示（秒，带小数） | ✅/⚠️/❌ | |
| 3 | 分拣状态显示（含保护状态） | ✅/⚠️/❌ | |
| 4 | 目标地点显示（T01~T10/无数据/未知） | ✅/⚠️/❌ | |
| 5 | 丝杆位置显示（cm，1位小数） | ✅/⚠️/❌ | |
| 6 | QR 累计计数显示 | ✅/⚠️/❌ | |

#### (b) 测试步骤

| 功能 | 测试方法 | 预期结果 |
|------|---------|---------|
| 丝杆位置 | BLE 连接后对比 OLED 与 APP 显示值 | 两端数值一致，误差 ≤ 0.1 cm |
| 分拣状态 | 扫描 QR 码后观察 APP 状态区 | 状态依次切换 等待→延时→移动→分拣→回原点→等待 |
| 目标地点 | 扫描 QR 码 0x03，观察 APP | 显示 "T04" |
| 系统使能 | 按下板载 KEY0，观察 APP | 使能状态切换，颜色变化 |
| 运行时间 | 设备运行约 10s 后对比 OLED | 两端时间一致（误差 ≤ 0.1s） |
| 保护状态 | 触发超温/过流后观察 APP | 状态显示红色"超温保护"或"过流保护" |

---

*文档版本：v1.0（STM32 TX 从 remoteVar_TX[0] 开始，对应 APP RX CMD 0x17~0x18）*
