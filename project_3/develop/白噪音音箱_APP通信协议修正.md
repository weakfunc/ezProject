# 白噪音音箱 — APP 通信协议修正说明

> 请根据本文档修正 MainActivity.kt 中 HomeScreen() 的数据读取和发送逻辑，使 APP 端与 STM32 端协议完全对齐。

---

## 一、APP 接收（显示用）— 从 `BleProtocol.rxFrames` 读取

| 数据含义 | APP RX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_TX 索引 |
|---|:---:|:---:|---|:---:|
| 定时剩余秒数 | 0x17 | var4b1 | Int | [0] |
| （保留） | 0x17 | var4b2 | — | [1] |
| 当前曲目编号（1~15） | 0x17 | var1b1 | Int | [2] |
| 播放状态（0=停止 1=播放 2=暂停） | 0x17 | var1b2 | Int | [3] |
| （保留） | 0x18 | var4b1 | — | [4] |
| （保留） | 0x18 | var4b2 | — | [5] |
| 当前音量（0~100） | 0x18 | var1b1 | Int | [6] |
| 当前场景模式（0=手动 1=助眠 2=专注 3=冥想 4=哄睡） | 0x18 | var1b2 | Int | [7] |
| （保留） | 0x19 | var4b1 | — | [8] |
| （保留） | 0x19 | var4b2 | — | [9] |
| 夜灯亮度（0~100） | 0x19 | var1b1 | Int | [10] |
| 定时设定分钟数（0/5/10/15/30/60） | 0x19 | var1b2 | Int | [11] |

### 读取代码

```kotlin
// 帧0 (CMD=0x17)
val frame17 = BleProtocol.rxFrames[0x17]
val timerRemainSec = frame17?.var4b1 ?: 0       // 定时剩余秒数
// var4b2 保留
val currentTrack   = frame17?.var1b1 ?: 1       // 当前曲目编号 1~15
val playState      = frame17?.var1b2 ?: 0       // 0=停止 1=播放 2=暂停

// 帧1 (CMD=0x18)
val frame18 = BleProtocol.rxFrames[0x18]
// var4b1 保留
// var4b2 保留
val volume         = frame18?.var1b1 ?: 50      // 音量 0~100
val sceneMode      = frame18?.var1b2 ?: 0       // 场景模式

// 帧2 (CMD=0x19)
val frame19 = BleProtocol.rxFrames[0x19]
// var4b1 保留
// var4b2 保留
val lightBrightness = frame19?.var1b1 ?: 0      // 灯光亮度 0~100
val timerSetMin     = frame19?.var1b2 ?: 0      // 定时设定分钟
```

---

## 二、APP 发送（控制用）— 通过 `BleProtocol.buildTxFrame()` 构建

| 操作含义 | APP TX CMD | 槽位 | 数据类型 | 对应 STM32 remoteVar_RX 索引 |
|---|:---:|:---:|---|:---:|
| 设定音量 | 0x21 | var4_1 | Int（0~100，0xFFFFFFFF=未操作） | [0] |
| 设定灯光亮度 | 0x21 | var4_2 | Int（0~100，0xFFFFFFFF=未操作） | [1] |
| 按键：播放/暂停 | 0x21 | var1_1 | Int（按下=1 释放=0） | [2] |
| 按键：下一曲 | 0x21 | var1_2 | Int（按下=1） | [3] |
| 设定定时分钟数 | 0x22 | var4_1 | Int（0/5/10/15/30/60，0xFFFFFFFF=未操作） | [4] |
| （保留） | 0x22 | var4_2 | — | [5] |
| 按键：模式切换 | 0x22 | var1_1 | Int（按下=1） | [6] |
| 按键：上一曲 | 0x22 | var1_2 | Int（按下=1） | [7] |

### 发送辅助函数

```kotlin
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
```

### 各操作的发送示例

```kotlin
// 设定音量为 75（仅改音量，其他槽位不影响）
sendFrame(cmd = 0x21, var4_1 = 75)

// 设定灯光亮度为 40（仅改灯光，其他槽位不影响）
sendFrame(cmd = 0x21, var4_2 = 40)

// 播放/暂停（按下 + 延迟 + 释放）
coroutineScope.launch {
    sendFrame(cmd = 0x21, var1_1 = 1)
    delay(100)
    sendFrame(cmd = 0x21, var1_1 = 0)
}

// 下一曲
coroutineScope.launch {
    sendFrame(cmd = 0x21, var1_2 = 1)
    delay(100)
    sendFrame(cmd = 0x21, var1_2 = 0)
}

// 上一曲
coroutineScope.launch {
    sendFrame(cmd = 0x22, var1_2 = 1)
    delay(100)
    sendFrame(cmd = 0x22, var1_2 = 0)
}

// 模式切换
coroutineScope.launch {
    sendFrame(cmd = 0x22, var1_1 = 1)
    delay(100)
    sendFrame(cmd = 0x22, var1_1 = 0)
}

// 设定定时 30 分钟
sendFrame(cmd = 0x22, var4_1 = 30)

// 取消定时
sendFrame(cmd = 0x22, var4_1 = 0)
```

---

## 三、注意事项

1. **按键类操作**（播放/暂停、上一曲、下一曲、模式切换）需要发送"按下=1"后延迟 100ms 再发送"释放=0"，STM32 端通过上升沿检测触发
2. **设定类操作**（音量、灯光、定时）直接发送目标值，STM32 端判断不等于 0xFFFFFFFF 时生效
3. **同一帧的多个槽位**：`sendFrame(cmd=0x21, var4_1=75)` 只设定 var4_1=75，其他参数默认为 0。如果 STM32 端按键也在 0x21 帧中，发送设定音量时 var1_1 和 var1_2 会被置 0——这不影响功能，因为 STM32 端按键用上升沿检测（0→1 才触发），0 值不会误触发
4. **数据刷新无需手动处理**：`BleProtocol.rxFrames` 是 `mutableStateOf`，Compose UI 读取时自动重组
