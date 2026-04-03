# 白噪音疗愈音箱 — APP 软件需求文档

> 本文档直接发送给 APP 项目目录下的 Claude Code 执行。
> Claude Code 拿到本文档 + 项目源码即可完成全部开发。

---

## 一、项目概述

为白噪音疗愈音箱开发手机 APP 遥控界面，实现以下功能：
1. 实时显示音箱状态（当前曲目、音量、播放状态、模式、定时、灯光亮度）
2. 远程控制播放（播放/暂停、上一曲/下一曲）
3. 远程调节音量和灯光亮度（滑块）
4. 远程切换场景模式（助眠/专注/冥想/儿童哄睡）
5. 远程设定定时关闭

---

## 二、开发边界

- **只修改** `MainActivity.kt` 中的 `HomeScreen()` 函数
- **不修改** BleProtocol.kt、BleConnectionManager.kt、BleAutoConnector.kt 等通信底层文件
- UI 使用 **Jetpack Compose + Material Design 3**
- 可复用现有组件：`HomeSection(title) { ... }` 和 `ProtoFieldRow(label1, value1, label2, value2)`

---

## 三、通信变量映射表

### APP 接收（显示用）— 从 `BleProtocol.rxFrames` 读取

| 数据含义 | APP RX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_TX 索引 |
|---|:---:|:---:|---|:---:|
| 定时剩余秒数 | 0x17 | var4b1 | uint32→Int | [0] |
| （保留） | 0x17 | var4b2 | — | [1] |
| 当前曲目编号 | 0x17 | var1b1 | uint8→Int | [2] |
| 播放状态 | 0x17 | var1b2 | uint8→Int (0=停止 1=播放 2=暂停) | [3] |
| （保留） | 0x18 | var4b1 | — | [4] |
| （保留） | 0x18 | var4b2 | — | [5] |
| 当前音量 | 0x18 | var1b1 | uint8→Int (0~100) | [6] |
| 当前场景模式 | 0x18 | var1b2 | uint8→Int (0=手动 1=助眠 2=专注 3=冥想 4=哄睡) | [7] |
| （保留） | 0x19 | var4b1 | — | [8] |
| （保留） | 0x19 | var4b2 | — | [9] |
| 夜灯亮度 | 0x19 | var1b1 | uint8→Int (0~100) | [10] |
| 定时设定分钟数 | 0x19 | var1b2 | uint8→Int (0/5/10/15/30/60) | [11] |

### 读取代码示例

```kotlin
// 帧0 (CMD=0x17)
val frame17 = BleProtocol.rxFrames[0x17]
val timerRemainSec = frame17?.var4b1 ?: 0       // 定时剩余秒数
val currentTrack   = frame17?.var1b1 ?: 1       // 当前曲目编号 1~15
val playState      = frame17?.var1b2 ?: 0       // 0=停止 1=播放 2=暂停

// 帧1 (CMD=0x18)
val frame18 = BleProtocol.rxFrames[0x18]
val volume         = frame18?.var1b1 ?: 50      // 音量 0~100
val sceneMode      = frame18?.var1b2 ?: 0       // 场景模式

// 帧2 (CMD=0x19)
val frame19 = BleProtocol.rxFrames[0x19]
val lightBrightness = frame19?.var1b1 ?: 0      // 灯光亮度 0~100
val timerSetMin     = frame19?.var1b2 ?: 0      // 定时设定分钟
```

### APP 发送（控制用）

| 操作含义 | APP TX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_RX 索引 |
|---|:---:|:---:|---|:---:|
| 设定音量 | 0x21 | var4_1 | uint32 (0~100, 0xFFFFFFFF=未操作) | [0] |
| 设定灯光亮度 | 0x21 | var4_2 | uint32 (0~100, 0xFFFFFFFF=未操作) | [1] |
| 按键：播放/暂停 | 0x21 | var1_1 | uint8 (按下=1 释放=0) | [2] |
| 按键：下一曲 | 0x21 | var1_2 | uint8 (按下=1) | [3] |
| 设定定时分钟 | 0x22 | var4_1 | uint32 (0/5/10/15/30/60, 0xFFFFFFFF=未操作) | [4] |
| （保留） | 0x22 | var4_2 | — | [5] |
| 按键：模式切换 | 0x22 | var1_1 | uint8 (按下=1) | [6] |
| 按键：上一曲 | 0x22 | var1_2 | uint8 (按下=1) | [7] |

### 发送代码示例

```kotlin
// 辅助函数：发送帧
fun sendFrame(cmd: Int, var4_1: Int = 0, var4_2: Int = 0, var1_1: Int = 0, var1_2: Int = 0) {
    val frame = BleProtocol.buildTxFrame(
        cmd = cmd, var4_1 = var4_1, var4_2 = var4_2, var1_1 = var1_1, var1_2 = var1_2
    )
    val ok = BleConnectionManager.writeCharacteristic(
        serviceUuid = UserConfig.esp32_service_1_uuid,
        characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
        value = frame
    )
    if (ok) BleConnectionManager.recordOutgoingMessage(
        characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
        value = frame
    )
}

// 播放/暂停按钮点击
sendFrame(cmd = 0x21, var1_1 = 1)
// 按钮释放（延迟100ms后发送）
sendFrame(cmd = 0x21, var1_1 = 0)

// 设定音量为 75
sendFrame(cmd = 0x21, var4_1 = 75)

// 设定灯光亮度为 40
sendFrame(cmd = 0x21, var4_2 = 40)

// 设定定时 30 分钟
sendFrame(cmd = 0x22, var4_1 = 30)

// 下一曲
sendFrame(cmd = 0x21, var1_2 = 1)
// 释放
sendFrame(cmd = 0x21, var1_2 = 0)

// 模式切换
sendFrame(cmd = 0x22, var1_1 = 1)
// 释放
sendFrame(cmd = 0x22, var1_1 = 0)
```

**按钮发送模式说明**：按钮类操作需要发送"按下"(=1)后短暂延迟再发送"释放"(=0)，STM32 端检测上升沿触发。可用 `LaunchedEffect` + `delay(100)` 实现。

---

## 四、UI 界面设计

`HomeScreen()` 布局设计，使用 `HomeSection` 划分功能区域：

### 区域1：状态显示
```
HomeSection(title = "当前状态") {
    Row: 曲目名称（中文）    播放状态图标（▶/⏸/⏹）
    Row: 音量 XX%           场景模式名称
}
```

**曲目名称映射**（APP 端维护）：
```kotlin
val trackNames = mapOf(
    1 to "雨声", 2 to "森林", 3 to "海浪", 4 to "溪流", 5 to "白噪",
    6 to "咖啡馆", 7 to "鸟鸣", 8 to "风声", 9 to "篝火", 10 to "雷雨",
    11 to "虫鸣", 12 to "钟声", 13 to "水滴", 14 to "城市", 15 to "列车"
)

val modeNames = mapOf(
    0 to "手动", 1 to "助眠", 2 to "专注", 3 to "冥想", 4 to "儿童哄睡"
)
```

### 区域2：播放控制
```
HomeSection(title = "播放控制") {
    Row（居中）: [⏮上一曲]  [⏯播放/暂停]  [⏭下一曲]
}
```
- 三个按钮水平排列，播放/暂停按钮根据 `playState` 切换图标和文字
- `playState==1` 时显示暂停图标，其他显示播放图标

### 区域3：音量调节
```
HomeSection(title = "音量") {
    Slider: 0~100，步进5，显示当前值 "XX%"
}
```
- Slider 的 `value` 绑定从 rxFrames 读取的 `volume`
- `onValueChangeFinished` 时发送 `sendFrame(cmd=0x21, var4_1=newVolume)`
- 注意：不要在 `onValueChange`（拖动过程中）频繁发送，只在松手时发送一次

### 区域4：灯光调节
```
HomeSection(title = "氛围灯") {
    Slider: 0~100，步进5，显示 "XX%" 或 "关"
}
```
- 同音量逻辑，松手时发送 `sendFrame(cmd=0x21, var4_2=newBrightness)`

### 区域5：场景模式
```
HomeSection(title = "场景模式") {
    Row: [助眠] [专注] [冥想] [哄睡]
}
```
- 四个按钮，当前激活的模式高亮（`MaterialTheme.colorScheme.primary`），其他灰色
- 点击已激活的模式按钮 → 退出模式（发送切换到模式=0）
- 点击未激活的模式按钮 → 切换到该模式

**模式切换发送逻辑**：APP 不直接设定模式编号，而是发送"模式切换"按钮事件，STM32 端循环切换。所以如果需要从模式0直接跳到模式2，需要发送多次按键。**简化方案**：每次点击都发送一次按键事件，UI 根据接收到的 `sceneMode` 更新高亮状态（可能有 500ms 延迟）。

### 区域6：定时设置
```
HomeSection(title = "定时关闭") {
    Row: [5分] [10分] [15分] [30分] [60分] [关]
    Text: "剩余: XX:XX" 或 "未设定"
}
```
- 选项按钮组，当前选中项高亮
- 点击后发送 `sendFrame(cmd=0x22, var4_1=selectedMinutes)`（0=关闭定时）
- 剩余时间从 `timerRemainSec` 计算，格式 `MM:SS`

---

## 五、交互逻辑

### 数据刷新
- `BleProtocol.rxFrames` 是 `mutableStateOf`，Compose 读取时自动重组，无需手动定时器
- 所有显示数据直接在 Composable 函数中读取 rxFrames

### 按钮状态管理
- 播放/暂停按钮：根据 `playState` 动态切换文字（"播放"/"暂停"）和图标
- 场景模式按钮：根据 `sceneMode` 高亮对应按钮
- 定时按钮：根据 `timerSetMin` 高亮对应选项

### 按键发送与释放
所有按钮类控制（播放/暂停、上一曲、下一曲、模式切换）需要发送"按下+释放"：
```kotlin
Button(onClick = {
    coroutineScope.launch {
        sendFrame(cmd = 0x21, var1_1 = 1)  // 按下
        delay(100)
        sendFrame(cmd = 0x21, var1_1 = 0)  // 释放
    }
})
```

### 滑块发送节流
音量和灯光滑块仅在 `onValueChangeFinished` 时发送，避免拖动过程中频繁通信。

### 无连接状态
- 当蓝牙未连接时，所有控制按钮变灰不可点击
- 可通过检查 `BleConnectionManager` 的连接状态来判断（阅读现有代码中的连接状态检查方式）

---

## 六、输出物要求

### 1. 代码文件

- 修改后的 `MainActivity.kt`（在修改区域添加注释标注 `// [新增] 白噪音音箱控制 UI`）

### 2. 功能实现文档（app_implementation_report.md）

**(a) 功能实现状态清单**

| 序号 | 功能描述 | 状态 | 说明 |
|:---:|---|:---:|---|
| 1 | 状态显示（曲目、音量、模式、播放状态） | ✅/⚠️/❌ | |
| 2 | 播放/暂停/上一曲/下一曲按钮 | ✅/⚠️/❌ | |
| 3 | 音量滑块调节 | ✅/⚠️/❌ | |
| 4 | 灯光亮度滑块调节 | ✅/⚠️/❌ | |
| 5 | 场景模式选择 | ✅/⚠️/❌ | |
| 6 | 定时设置与剩余时间显示 | ✅/⚠️/❌ | |
| 7 | 无连接状态 UI 处理 | ✅/⚠️/❌ | |

**(b) 已实现功能的测试步骤**

```
功能1：状态显示
  测试步骤：
    1. STM32 音箱上电，手机 APP 蓝牙连接
    2. 在串口屏上切换曲目，观察 APP 显示的曲目名是否同步更新
    3. 在串口屏上调节音量，观察 APP 显示的音量值是否同步
    4. 切换场景模式，观察 APP 模式名称是否更新
  预期结果：APP 显示与音箱实际状态一致，延迟 < 1s

功能2：播放控制
  测试步骤：
    1. APP 点击播放按钮，观察音箱开始播放
    2. APP 点击暂停按钮，观察音箱暂停
    3. APP 点击下一曲，观察曲目切换
    4. APP 点击上一曲，观察曲目切换
  预期结果：APP 操作与串口屏操作效果一致

功能3：音量滑块
  测试步骤：
    1. 拖动音量滑块到 75%，松手
    2. 观察音箱音量变化
    3. 观察串口屏 t10 显示更新为 "75"
  预期结果：松手后音量生效，串口屏同步

功能4：灯光滑块
  测试步骤：
    1. 拖动灯光滑块到 40%，松手
    2. 观察 LED 灯亮度变化
    3. 滑到 0%，观察灯光关闭
  预期结果：亮度与滑块值一致

功能5：场景模式
  测试步骤：
    1. 点击"助眠"按钮，观察按钮高亮 + 音箱切换到助眠模式
    2. 点击"专注"按钮，观察切换
    3. 再点击已高亮的按钮，观察是否退回手动模式
  预期结果：模式正确切换，UI 高亮同步

功能6：定时设置
  测试步骤：
    1. 点击 "30分" 按钮
    2. 观察剩余时间显示开始倒计时 "29:59..."
    3. 点击 "关" 按钮，观察定时取消
  预期结果：定时设定生效，倒计时正确显示
```
