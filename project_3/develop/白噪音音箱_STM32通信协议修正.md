# 白噪音音箱 — STM32 通信协议升级需求

> 本文档发给 STM32 项目目录下的 Claude Code 执行。
> 目标：将 APP 通信从"CMD+参数+序号"旧协议改为"固定槽位映射"新协议。

---

## 一、绝对禁止修改的文件

**以下文件不可做任何修改，一个字节都不能动：**

- `func_appcom.c` / `func_appcom.h`
- `task_system.c` / `task_system.h`
- `userLib/` 目录下的所有文件
- `userDriver/` 目录下的所有文件（除非功能需要且在本文档第五节明确要求）
- `Core/` 目录下的所有文件

**原因**：func_appcom 中的帧 CMD 编号（TX: 0x09~0x0C，RX: 0x13~0x16）和帧数量（4TX+4RX）是框架与 ESP32 固件的固定约定，修改会导致 BLE 断连。

---

## 二、需要修改的文件

仅修改：
- `task_user1.c`
- `task_user1.h`

---

## 三、帧号与 remoteVar 索引的映射关系（只读参考，不可更改）

以下映射由 func_appcom 框架固定，task 层通过 `remoteVar_TX[索引]` / `remoteVar_RX[索引]` 读写即可，无需关心底层帧号：

**TX（STM32 → APP）**：

| remoteVar_TX 索引 | 底层帧 CMD | 帧内槽位 | 有效宽度 |
|:---:|:---:|:---:|:---:|
| [0] | 0x09 | var_4b_1 | 4B |
| [1] | 0x09 | var_4b_2 | 4B |
| [2] | 0x09 | var_1b_1 | **1B** |
| [3] | 0x09 | var_1b_2 | **1B** |
| [4] | 0x0A | var_4b_1 | 4B |
| [5] | 0x0A | var_4b_2 | 4B |
| [6] | 0x0A | var_1b_1 | **1B** |
| [7] | 0x0A | var_1b_2 | **1B** |
| [8] | 0x0B | var_4b_1 | 4B |
| [9] | 0x0B | var_4b_2 | 4B |
| [10] | 0x0B | var_1b_1 | **1B** |
| [11] | 0x0B | var_1b_2 | **1B** |
| [12]~[15] | 0x0C | 同上规律 | — |

**RX（APP → STM32）**：

| remoteVar_RX 索引 | 底层帧 CMD | 帧内槽位 | 有效宽度 |
|:---:|:---:|:---:|:---:|
| [0] | 0x13 | var_4b_1 | 4B |
| [1] | 0x13 | var_4b_2 | 4B |
| [2] | 0x13 | var_1b_1 | **1B** |
| [3] | 0x13 | var_1b_2 | **1B** |
| [4] | 0x14 | var_4b_1 | 4B |
| [5] | 0x14 | var_4b_2 | 4B |
| [6] | 0x14 | var_1b_1 | **1B** |
| [7] | 0x14 | var_1b_2 | **1B** |
| [8]~[15] | 0x15~0x16 | 同上规律 | — |

经过 ESP32 转发后，APP 端看到的 CMD 编号会偏移：
- STM32 TX 0x09 → APP RX **0x17**
- STM32 TX 0x0A → APP RX **0x18**
- STM32 TX 0x0B → APP RX **0x19**
- STM32 TX 0x0C → APP RX **0x1A**
- APP TX 0x21 → STM32 RX **0x13**
- APP TX 0x22 → STM32 RX **0x14**
- APP TX 0x23 → STM32 RX **0x15**
- APP TX 0x24 → STM32 RX **0x16**

**task 层不需要知道这些偏移，只管用 remoteVar_TX[0]~[15] 和 remoteVar_RX[0]~[15] 读写即可。**

---

## 四、新协议变量布局

### TX（STM32 → APP）— 状态上报

| remoteVar_TX 索引 | 有效宽度 | 变量含义 | 数据类型 |
|:---:|:---:|---|---|
| [0] | 4B | 定时剩余秒数 | var_uint32 |
| [1] | 4B | （保留） | — |
| [2] | 1B | 当前曲目编号（1~15） | var_uint32 低8位 |
| [3] | 1B | 播放状态（0=停止 1=播放 2=暂停） | var_uint32 低8位 |
| [4] | 4B | （保留） | — |
| [5] | 4B | （保留） | — |
| [6] | 1B | 当前音量（0~100） | var_uint32 低8位 |
| [7] | 1B | 当前场景模式（0=手动 1=助眠 2=专注 3=冥想 4=哄睡） | var_uint32 低8位 |
| [8] | 4B | （保留） | — |
| [9] | 4B | （保留） | — |
| [10] | 1B | 夜灯亮度（0~100） | var_uint32 低8位 |
| [11] | 1B | 定时设定分钟数（0/5/10/15/30/60） | var_uint32 低8位 |
| [12]~[15] | — | （保留） | — |

### RX（APP → STM32）— 控制指令

| remoteVar_RX 索引 | 有效宽度 | 变量含义 | 数据类型 |
|:---:|:---:|---|---|
| [0] | 4B | 设定音量（0~100，0xFFFFFFFF=未操作） | var_uint32 |
| [1] | 4B | 设定灯光亮度（0~100，0xFFFFFFFF=未操作） | var_uint32 |
| [2] | 1B | 按键：播放/暂停（按下=1 释放=0） | var_uint32 低8位 |
| [3] | 1B | 按键：下一曲（按下=1） | var_uint32 低8位 |
| [4] | 4B | 设定定时分钟（0/5/10/15/30/60，0xFFFFFFFF=未操作） | var_uint32 |
| [5] | 4B | （保留） | — |
| [6] | 1B | 按键：模式切换（按下=1） | var_uint32 低8位 |
| [7] | 1B | 按键：上一曲（按下=1） | var_uint32 低8位 |
| [8]~[15] | — | （保留） | — |

---

## 五、task_user1.h 修改要求

删除旧协议相关的枚举/结构体（如 APP 指令码枚举、指令参数结构体等），替换为：

```c
// ---- APP 按键上升沿检测历史值 ----
// 在 user1TaskInfo_t 结构体中，确保有以下字段：
uint8_t appKeyPrev[4];   // [0]=播放暂停 [1]=下一曲 [2]=模式切换 [3]=上一曲
```

其他字段（playState、currentTrack、volume、sceneMode、lightBrightness、timerRemainSec、timerSetMinutes、screenNeedUpdate 等）保持不变。

---

## 六、task_user1.c 修改要求

### 6.1 Init 函数

在 `user1TaskInit()` 中，初始化 APP 按键历史值：

```c
memset(user1TaskInfo.appKeyPrev, 0, sizeof(user1TaskInfo.appKeyPrev));
```

**不要在 Init 中写 remoteVar_RX**（不要预填 0xFFFFFFFF），这些值由 APP 端控制。

### 6.2 TX 发送（替换旧的 APP 状态上报逻辑）

在 500ms 周期中执行（`taskCnt % 250 == 0`）：

```c
// ===== TX：发送状态到 APP =====
remoteInfo.remoteVar_TX[0].var_uint32  = user1TaskInfo.timerRemainSec;
// [1] 不写
remoteInfo.remoteVar_TX[2].var_uint32  = (uint32_t)user1TaskInfo.currentTrack;
remoteInfo.remoteVar_TX[3].var_uint32  = (uint32_t)user1TaskInfo.playState;
// [4][5] 不写
remoteInfo.remoteVar_TX[6].var_uint32  = (uint32_t)user1TaskInfo.volume;
remoteInfo.remoteVar_TX[7].var_uint32  = (uint32_t)user1TaskInfo.sceneMode;
// [8][9] 不写
remoteInfo.remoteVar_TX[10].var_uint32 = (uint32_t)user1TaskInfo.lightBrightness;
remoteInfo.remoteVar_TX[11].var_uint32 = (uint32_t)user1TaskInfo.timerSetMinutes;
// [12]~[15] 不写
```

**重要：不要在发送前把 remoteVar_TX 整体清零。** 只写需要的索引，不写的保持原值。整体清零会导致未使用的帧发送全零数据，可能影响 ESP32。

### 6.3 RX 接收（替换旧的 APP 指令解析逻辑）

在 500ms 周期中执行（`taskCnt % 250 == 0`），紧跟 TX 之后：

```c
// ===== RX：接收 APP 控制 =====

// --- [0] 音量设定 ---
uint32_t appVol = remoteInfo.remoteVar_RX[0].var_uint32;
if(appVol != 0xFFFFFFFF && appVol <= 100) {
    user1TaskInfo.volume = (uint8_t)appVol;
    // TODO: 调用 MP3 driver 设置音量
    user1TaskInfo.screenNeedUpdate = 1;
    remoteInfo.remoteVar_RX[0].var_uint32 = 0xFFFFFFFF;  // 消费后复位
}

// --- [1] 灯光亮度设定 ---
uint32_t appLight = remoteInfo.remoteVar_RX[1].var_uint32;
if(appLight != 0xFFFFFFFF && appLight <= 100) {
    user1TaskInfo.lightBrightness = (uint8_t)appLight;
    // TODO: 调用 LED driver 设置 PWM
    user1TaskInfo.screenNeedUpdate = 1;
    remoteInfo.remoteVar_RX[1].var_uint32 = 0xFFFFFFFF;  // 消费后复位
}

// --- [2] 播放/暂停按键（上升沿检测）---
uint8_t appKeyPlay = (uint8_t)(remoteInfo.remoteVar_RX[2].var_uint32 & 0xFF);
if(appKeyPlay == 1 && user1TaskInfo.appKeyPrev[0] == 0) {
    // TODO: 执行播放/暂停切换逻辑（与串口屏 b1 按钮逻辑相同）
    user1TaskInfo.screenNeedUpdate = 1;
}
user1TaskInfo.appKeyPrev[0] = appKeyPlay;

// --- [3] 下一曲按键（上升沿检测）---
uint8_t appKeyNext = (uint8_t)(remoteInfo.remoteVar_RX[3].var_uint32 & 0xFF);
if(appKeyNext == 1 && user1TaskInfo.appKeyPrev[1] == 0) {
    // TODO: 执行下一曲逻辑（与串口屏 b2 按钮逻辑相同）
    user1TaskInfo.screenNeedUpdate = 1;
}
user1TaskInfo.appKeyPrev[1] = appKeyNext;

// --- [4] 定时设定 ---
uint32_t appTimer = remoteInfo.remoteVar_RX[4].var_uint32;
if(appTimer != 0xFFFFFFFF) {
    if(appTimer == 0 || appTimer == 5 || appTimer == 10 ||
       appTimer == 15 || appTimer == 30 || appTimer == 60) {
        user1TaskInfo.timerSetMinutes = (uint16_t)appTimer;
        if(appTimer > 0) {
            user1TaskInfo.timerEnabled = 1;
            user1TaskInfo.timerRemainSec = appTimer * 60;
        } else {
            user1TaskInfo.timerEnabled = 0;
            user1TaskInfo.timerRemainSec = 0;
        }
        user1TaskInfo.screenNeedUpdate = 1;
    }
    remoteInfo.remoteVar_RX[4].var_uint32 = 0xFFFFFFFF;  // 消费后复位
}

// --- [5] 保留，不读取 ---

// --- [6] 模式切换按键（上升沿检测）---
uint8_t appKeyMode = (uint8_t)(remoteInfo.remoteVar_RX[6].var_uint32 & 0xFF);
if(appKeyMode == 1 && user1TaskInfo.appKeyPrev[2] == 0) {
    // TODO: 执行场景模式循环切换逻辑（与串口屏 b5 按钮逻辑相同）
    user1TaskInfo.screenNeedUpdate = 1;
}
user1TaskInfo.appKeyPrev[2] = appKeyMode;

// --- [7] 上一曲按键（上升沿检测）---
uint8_t appKeyPrevTrack = (uint8_t)(remoteInfo.remoteVar_RX[7].var_uint32 & 0xFF);
if(appKeyPrevTrack == 1 && user1TaskInfo.appKeyPrev[3] == 0) {
    // TODO: 执行上一曲逻辑（与串口屏 b0 按钮逻辑相同）
    user1TaskInfo.screenNeedUpdate = 1;
}
user1TaskInfo.appKeyPrev[3] = appKeyPrevTrack;
```

### 6.4 删除旧协议代码

删除 task_user1.c 中所有旧协议相关代码，包括但不限于：
- 旧的 CMD 指令码解析逻辑（switch/case 0x01、0x02 等）
- 旧的 APP 指令序号去重逻辑
- 旧的"CMD + param + seq"组包/解包代码
- 旧的 appSceneMode 额外维护逻辑（直接用 `user1TaskInfo.sceneMode` 上报）

---

## 七、关键注意事项

1. **不要修改 func_appcom.c/h** — 帧号（0x09~0x0C / 0x13~0x16）和帧数量（4+4）由框架固定
2. **不要整体清零 remoteVar_TX** — 只写需要的索引
3. **不要在 Init 中预填 remoteVar_RX** — RX 值由 APP 端通过 BLE 写入，STM32 端只读取和消费
4. **RX 消费后复位**：音量/灯光/定时这三个 4B 槽位，处理完后写回 `0xFFFFFFFF`，防止下个周期重复执行
5. **按键不需要复位**：1B 槽位的按键值由 APP 端自行发送 0（释放），STM32 端只做上升沿检测
6. **所有 TODO 注释**：替换为实际的 driver API 调用（与当前串口屏按钮触发的逻辑相同，复用同一个处理函数即可）

---

## 八、验证清单

修改完成后，请逐项确认：

- [ ] func_appcom.c/h 没有任何改动（`git diff func_appcom.c func_appcom.h` 应为空）
- [ ] task_user1.c/h 中无旧协议相关代码残留
- [ ] 编译零错误零警告
- [ ] `remoteVar_TX` 写入索引：0, 2, 3, 6, 7, 10, 11（不越界，不整体清零）
- [ ] `remoteVar_RX` 读取索引：0, 1, 2, 3, 4, 6, 7（不越界）
- [ ] `appKeyPrev` 数组大小为 4
- [ ] 没有在代码中出现 0x17、0x18、0x19、0x21、0x22 这些帧号
