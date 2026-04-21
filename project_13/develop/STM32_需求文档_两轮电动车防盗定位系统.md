# STM32 软件需求文档
# 两轮电动车智能防盗与定位系统

> 本文档供 STM32 项目目录下的 Claude Code 执行开发，拿到本文档 + 项目源码即可完成开发，无需其他文档。

---

## 一、项目概述

本项目基于 STM32F103C8T6 实现两轮电动车智能防盗与定位系统，核心功能如下：

1. **GPS 定位上报**：以 1s 周期读取 GPS 模块数据，解析经纬度、定位状态，通过 BLE 链路周期发送给手机 APP，APP 端负责实时位置显示和历史轨迹回放，STM32 不做 GPS 数据处理。
2. **震动检测防盗**：防盗功能开启时，使用陀螺仪加速度计数据融合判断是否发生震动，在 STM32 端处理，向 APP 提供报警标志位。
3. **位移监测防盗**：防盗功能开启时，基于 GPS 坐标计算与初始位置的位移距离，超过 10m 则触发报警。
4. **声光报警**：震动或位移异常触发后，通过板载蜂鸣器和 LED 灯执行本地声光报警，同时通过 APP 通信上报报警标志。
5. **OLED 显示**：显示 GPS 状态、GPS 经纬度坐标（大字体显示具体数值）、休眠倒计时（30s）。
6. **防盗开关远程控制**：APP 可远程开启/关闭防盗功能，APP 可远程消除报警。
7. **低功耗休眠**：无异常时进入低功耗休眠模式，任意 KEY 按键按下唤醒，唤醒后重置 30s 倒计时。

---

## 二、开发前置步骤

**Claude Code 必须首先执行以下步骤，再开始任何代码开发：**

1. 浏览 `MDK-ARM/userDriver/` 目录，列出所有 `.c/.h` 文件
2. 逐个阅读每个 driver 的 `.h` 文件中「向上提供」部分（公有结构体 `xxxInfo`、`DRIVER_XXX_Init()`、`DRIVER_XXX_Update()`、其他 API）
3. 汇总所有可用的结构体和 API，形成一份简短的 driver 清单
4. 以此为基础进行后续 task 层设计（不要假设 driver 名称，以实际文件为准）

> 本项目预期涉及的 driver 模块（以实际 userDriver/ 目录为准）：
> - GPS 模块（NMEA 解析，提供经纬度、定位状态）
> - 陀螺仪/加速度计（MPU-6050 或同类，提供加速度数据）
> - OLED 显示（提供字符/数字显示 API）
> - 蜂鸣器（提供鸣叫 API）
> - LED（提供开关/闪烁 API）
> - 按键（提供按键检测 API）

---

## 二（附）、禁止修改的文件清单

以下文件**严禁修改**，违反将导致系统异常：

| 文件 | 禁止原因 |
|------|---------|
| `func_appcom.c` / `func_appcom.h` | 帧 CMD 编号（TX: 0x09~0x0C，RX: 0x13~0x16）和帧数量（4TX+4RX）是框架与 ESP32 固件的**固定约定**，修改会导致 BLE 断连 |
| `task_system.c` / `task_system.h` | 系统任务，框架已实现，负责 stdlib 状态机维护和 appcom 通信更新 |
| `userLib/` 下所有文件 | stdlib 层，框架已实现，不可修改 |
| `Core/` 下所有文件 | HAL 层，CubeMX 生成，不可修改 |

**关键注意事项（必须遵守）：**

- `TX[0]~[3]`（帧 0x09）**不会到达 APP**，发送给 APP 的变量必须使用 `TX[4]~[15]`
- **不要整体清零** `remoteVar_TX`，只写需要的索引，未使用的保持原值
- **不要在代码中出现** `0x17~0x1A`、`0x21~0x24` 等 APP 端帧号，STM32 端不需要知道这些
- task 层操作变量：`remoteInfo.remoteVar_TX[idx]` 和 `remoteInfo.remoteVar_RX[idx]`，`FUNC_APPCOM_UPDATA()` 由 `task_system` 自动调用，task 层不调用

---

## 三、task 层设计

### 3.1 文件规划

仅使用 `task_user1.c / task_user1.h`，所有应用逻辑在此实现。

### 3.2 公有结构体设计

```c
// task_user1.h
typedef struct {
    uint32_t taskCnt;           // 任务计数器，用于子周期取余

    /* GPS 数据 */
    float gpsLatitude;          // 纬度（度，浮点）
    float gpsLongitude;         // 经度（度，浮点）
    uint8_t gpsValid;           // GPS 定位状态（0=无效，1=有效）

    /* 防盗状态 */
    uint8_t antitheftEnabled;   // 防盗功能开关（0=关闭，1=开启）
    uint8_t alarmFlag;          // 报警标志（0=正常，1=报警中）
    uint8_t alarmSource;        // 报警来源（0=无，1=震动，2=位移）

    /* 位移监测 */
    float refLatitude;          // 防盗开启时记录的参考纬度
    float refLongitude;         // 防盗开启时记录的参考经度
    uint8_t refPositionSet;     // 参考位置是否已记录（0=未记录，1=已记录）

    /* 休眠管理 */
    uint32_t sleepCountdownMs;  // 休眠倒计时剩余毫秒数（最大 30000）
    uint8_t isSleeping;         // 当前是否处于休眠状态（0=正常，1=休眠）

    /* APP 远程指令缓存 */
    uint8_t appAntitheftCmd;    // APP 发来的防盗开关指令（读取后处理）
    uint8_t appAlarmClearCmd;   // APP 发来的报警消除指令（读取后处理）

} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;
void user1TaskInit(void);
void user1TaskUpdata(void *argument);
```

### 3.3 Init 函数设计

```c
void user1TaskInit(void) {
    /* 初始化所有相关 driver（函数名以实际 driver .h 为准） */
    DRIVER_GPS_Init();
    DRIVER_IMU_Init();       // 陀螺仪/加速度计
    DRIVER_OLED_Init();
    DRIVER_BUZZER_Init();
    DRIVER_LED_Init();
    DRIVER_KEY_Init();

    /* 初始化任务内部状态 */
    user1TaskInfo.taskCnt          = 0;
    user1TaskInfo.gpsLatitude      = 0.0f;
    user1TaskInfo.gpsLongitude     = 0.0f;
    user1TaskInfo.gpsValid         = 0;
    user1TaskInfo.antitheftEnabled = 0;
    user1TaskInfo.alarmFlag        = 0;
    user1TaskInfo.alarmSource      = 0;
    user1TaskInfo.refLatitude      = 0.0f;
    user1TaskInfo.refLongitude     = 0.0f;
    user1TaskInfo.refPositionSet   = 0;
    user1TaskInfo.sleepCountdownMs = 30000;
    user1TaskInfo.isSleeping       = 0;
    user1TaskInfo.appAntitheftCmd  = 0xFF; // 0xFF 表示无指令
    user1TaskInfo.appAlarmClearCmd = 0;
}
```

### 3.4 Updata 循环设计

基础周期 2ms，通过 `taskCnt` 取余实现子周期。

#### 周期规划

| 子周期 | taskCnt 取余 | 负责功能 |
|--------|-------------|---------|
| 2ms | 每次 | 按键检测、休眠唤醒处理 |
| 10ms | % 5 | driver Update（GPS、IMU 等需周期更新的模块） |
| 100ms | % 50 | 震动检测算法、声光报警控制、APP RX 指令处理、APP TX 数据写入 |
| 500ms | % 250 | OLED 刷新（包含休眠倒计时） |
| 1000ms | % 500 | GPS 数据读取并解析、位移计算、GPS TX 数据写入 |

#### 主循环结构

```c
void user1TaskUpdata(void *argument) {
    user1TaskInit();
    for (;;) {
        user1TaskInfo.taskCnt++;

        /* 2ms：按键检测 & 休眠唤醒 */
        // 见功能详细需求 5.7

        /* 10ms：driver Update */
        if (user1TaskInfo.taskCnt % 5 == 0) {
            DRIVER_GPS_Update();
            DRIVER_IMU_Update();
            DRIVER_KEY_Update();
        }

        /* 100ms：防盗检测、报警控制、APP 通信 */
        if (user1TaskInfo.taskCnt % 50 == 0) {
            // APP RX 指令处理（见 5.5）
            // 震动检测（见 5.3）
            // 报警声光控制（见 5.4）
            // APP TX 写入（见 5.6）
        }

        /* 500ms：OLED 刷新 */
        if (user1TaskInfo.taskCnt % 250 == 0) {
            // OLED 刷新（见 5.8）
        }

        /* 1000ms：GPS 读取 & 位移计算 */
        if (user1TaskInfo.taskCnt % 500 == 0) {
            // GPS 数据更新（见 5.1）
            // 位移监测（见 5.2）
        }

        osDelay(2);
    }
}
```

---

## 四、APP 通信变量布局

### TX（STM32 → APP，仅 [4]~[15] 对 APP 可见）

| remoteVar_TX 索引 | 有效宽度 | 变量含义 | 数据类型 | 赋值方式 |
|------------------|---------|---------|---------|---------|
| [4] | 4B | GPS 纬度 | float | `var_float = gpsLatitude` |
| [5] | 4B | GPS 经度 | float | `var_float = gpsLongitude` |
| [6] | 1B | GPS 定位状态（0=无效，1=有效） | uint8 | `var_uint32 = gpsValid` |
| [7] | 1B | 防盗报警标志（0=正常，1=报警） | uint8 | `var_uint32 = alarmFlag` |
| [8] | 4B | GPS 帧序号（uint32，每次发送递增，供 APP 轨迹排序） | uint32 | `var_uint32 = gpsFrameSeq++` |
| [9] | 4B | 暂留，保持原值 | — | 不写 |
| [10] | 1B | 防盗开关当前状态（0=关闭，1=开启） | uint8 | `var_uint32 = antitheftEnabled` |
| [11] | 1B | 系统状态（0=正常，1=休眠，2=报警中） | uint8 | `var_uint32 = systemStatus` |
| [12]~[15] | — | 暂留，保持原值 | — | 不写 |

> TX[0]~[3] 不转发给 APP，不用于 APP 通信。

### RX（APP → STM32，全部可用）

| remoteVar_RX 索引 | 有效宽度 | 变量含义 | 数据类型 | 读取方式 |
|------------------|---------|---------|---------|---------|
| [0] | 4B | 暂留 | — | 不读 |
| [1] | 4B | 暂留 | — | 不读 |
| [2] | 1B | 防盗开关指令（0=关闭，1=开启） | uint8 | `var_uint32 & 0xFF` |
| [3] | 1B | 报警消除指令（写 1 触发，STM32 读取后清零 alarmFlag） | uint8 | `var_uint32 & 0xFF` |
| [4]~[7] | — | 暂留 | — | 不读 |

---

## 五、功能详细需求

### 5.1 GPS 数据读取与上报

**功能描述：** 以 1s 周期从 GPS driver 读取最新数据，更新任务结构体，并写入 remoteVar_TX 供 APP 读取。

**实现逻辑（1000ms 子周期）：**
1. 调用 GPS driver API 获取最新经纬度和定位状态
2. 更新 `user1TaskInfo.gpsLatitude`、`gpsLongitude`、`gpsValid`
3. 写入 TX 变量：
   ```c
   static uint32_t gpsFrameSeq = 0;
   remoteInfo.remoteVar_TX[4].var_float   = user1TaskInfo.gpsLatitude;
   remoteInfo.remoteVar_TX[5].var_float   = user1TaskInfo.gpsLongitude;
   remoteInfo.remoteVar_TX[6].var_uint32  = (uint32_t)user1TaskInfo.gpsValid;
   remoteInfo.remoteVar_TX[8].var_uint32  = gpsFrameSeq++;
   ```
4. STM32 不做坐标解算或轨迹记录，APP 端负责处理。

**涉及 driver API：** 以 `userDriver/` 中实际 GPS driver 为准，读取 `gpsInfo.latitude`、`gpsInfo.longitude`、`gpsInfo.valid`（字段名以 driver .h 为准）。

### 5.2 位移监测防盗

**功能描述：** 防盗功能开启时，基于 GPS 坐标计算当前位置与参考位置的直线距离，超过 10m 触发报警。

**实现逻辑（1000ms 子周期，GPS 读取后执行）：**
1. 若防盗功能刚开启（`antitheftEnabled==1` 且 `refPositionSet==0`）且 GPS 有效，记录当前坐标为参考位置，置 `refPositionSet=1`
2. 若防盗已开启且参考位置已设定且 GPS 有效，调用以下函数计算距离：

```c
/* 私有函数，写在 task_user1.c 中 */
/* 用 Haversine 公式简化版（小距离近似），返回距离（米） */
static float calcDistanceM(float lat1, float lon1, float lat2, float lon2) {
    /* 地球半径 6371000m，角度转弧度 */
    float dlat = (lat2 - lat1) * 3.14159265f / 180.0f;
    float dlon = (lon2 - lon1) * 3.14159265f / 180.0f;
    float meanLat = (lat1 + lat2) / 2.0f * 3.14159265f / 180.0f;
    float dx = dlon * 6371000.0f * cosf(meanLat);
    float dy = dlat * 6371000.0f;
    return sqrtf(dx * dx + dy * dy);
}
```

3. 若距离 > 10.0f 米，设置 `alarmFlag=1`，`alarmSource=2`（位移报警）
4. 若防盗关闭，重置 `refPositionSet=0`（下次开启时重新记录参考位置）

**关键阈值：** 位移阈值 10m（`#define ANTITHEFT_DISPLACE_THRESHOLD_M  10.0f`）

### 5.3 震动检测防盗

**功能描述：** 防盗功能开启时，融合陀螺仪加速度计数据判断震动，在 STM32 端处理，向 APP 提供报警标志。

**实现逻辑（100ms 子周期）：**
1. 从 IMU driver 读取三轴加速度值（单位 g，以 driver API 为准）
2. 计算加速度合向量：`acc_total = sqrtf(ax*ax + ay*ay + az*az)`
3. 静止状态下合向量约为 1g，偏差超过阈值则判定为震动：
   - 若 `|acc_total - 1.0f| > VIBRATION_THRESHOLD` 则计数器 `vibrationCnt++`
   - 若 `vibrationCnt >= VIBRATION_CONFIRM_CNT`，判定为震动报警，设置 `alarmFlag=1`，`alarmSource=1`，重置计数器
   - 若无震动，计数器每次递减（下限为 0，防抖处理）
4. 防盗关闭时不执行震动检测，重置计数器

**关键参数：**
```c
#define VIBRATION_THRESHOLD     0.3f   /* 加速度偏差阈值（g），超过则判定为震动 */
#define VIBRATION_CONFIRM_CNT   3      /* 连续确认次数（100ms*3=300ms内持续震动才报警）*/
```

**涉及 driver API：** IMU driver 的加速度读取接口（以实际 driver .h 为准）

### 5.4 声光报警控制

**功能描述：** `alarmFlag==1` 时，驱动蜂鸣器鸣叫和 LED 闪烁；`alarmFlag==0` 时，停止声光输出。

**实现逻辑（100ms 子周期）：**
1. 若 `alarmFlag==1`：
   - 蜂鸣器交替鸣叫（每 100ms 切换一次开/关状态，即 200ms 周期）
   - LED 闪烁（每 100ms 切换一次）
2. 若 `alarmFlag==0`：
   - 调用蜂鸣器关闭 API
   - 调用 LED 关闭 API（或恢复正常状态）

**涉及 driver API：** 蜂鸣器和 LED 的开关 API（driver 层已实现，以实际 driver .h 为准）

### 5.5 APP 远程指令处理

**功能描述：** 读取 APP 发来的防盗开关指令和报警消除指令，更新本地状态。

**实现逻辑（100ms 子周期，最先执行）：**

```c
/* 读取防盗开关指令（RX[2]） */
uint8_t appAntitheftCmd = (uint8_t)(remoteInfo.remoteVar_RX[2].var_uint32 & 0xFF);
if (appAntitheftCmd == 0 || appAntitheftCmd == 1) {
    if (appAntitheftCmd != user1TaskInfo.antitheftEnabled) {
        user1TaskInfo.antitheftEnabled = appAntitheftCmd;
        if (appAntitheftCmd == 0) {
            /* 关闭防盗：重置参考位置，清除报警 */
            user1TaskInfo.refPositionSet = 0;
            user1TaskInfo.alarmFlag      = 0;
            user1TaskInfo.alarmSource    = 0;
        }
        /* 重置睡眠倒计时，防止刚操作就睡眠 */
        user1TaskInfo.sleepCountdownMs = 30000;
    }
}

/* 读取报警消除指令（RX[3]） */
uint8_t appAlarmClearCmd = (uint8_t)(remoteInfo.remoteVar_RX[3].var_uint32 & 0xFF);
if (appAlarmClearCmd == 1) {
    user1TaskInfo.alarmFlag  = 0;
    user1TaskInfo.alarmSource = 0;
    /* 注意：不要清零 RX 变量，只读取不修改 */
}
```

**字节序说明：** 直接使用 `var_uint32`，`func_appcom` 已完成大端→小端翻转，task 层禁止再做任何字节序处理。

### 5.6 APP TX 数据周期写入

**功能描述：** 每 100ms 将系统状态写入 remoteVar_TX，供 APP 读取。

**实现逻辑（100ms 子周期，最后执行）：**

```c
/* GPS 相关数据在 1s 子周期中写入，此处只写状态类变量 */
remoteInfo.remoteVar_TX[7].var_uint32  = (uint32_t)user1TaskInfo.alarmFlag;
remoteInfo.remoteVar_TX[10].var_uint32 = (uint32_t)user1TaskInfo.antitheftEnabled;

/* systemStatus：0=正常，1=休眠，2=报警中 */
uint8_t sysStatus = 0;
if (user1TaskInfo.isSleeping)          sysStatus = 1;
else if (user1TaskInfo.alarmFlag)      sysStatus = 2;
remoteInfo.remoteVar_TX[11].var_uint32 = (uint32_t)sysStatus;
```

> 直接赋值，无需处理字节序。TX[4]、TX[5]、TX[6]、TX[8] 在 1s GPS 子周期中写入，不要在此重复写入。

### 5.7 休眠与唤醒

**功能描述：** 无异常（`alarmFlag==0`）时，30s 倒计时归零后进入低功耗休眠；任意 KEY 按键按下后唤醒，重置 30s 倒计时。

**实现逻辑：**

**倒计时（每 2ms 执行一次）：**
```c
/* 仅在正常状态下倒计时 */
if (!user1TaskInfo.isSleeping && user1TaskInfo.alarmFlag == 0) {
    if (user1TaskInfo.sleepCountdownMs > 2) {
        user1TaskInfo.sleepCountdownMs -= 2;
    } else {
        user1TaskInfo.sleepCountdownMs = 0;
        /* 进入休眠：调用休眠 API（以实际 driver/stdlib API 为准）*/
        user1TaskInfo.isSleeping = 1;
        /* 进入低功耗模式，等待按键中断唤醒 */
        /* 具体休眠 API 以项目框架提供的 API 为准 */
    }
}
```

**按键唤醒（每 2ms 或 10ms 周期中执行）：**
```c
/* 任意按键按下，重置倒计时并唤醒 */
if (DRIVER_KEY_GetPressed() != KEY_NONE) {   /* API 名称以实际 driver 为准 */
    user1TaskInfo.isSleeping       = 0;
    user1TaskInfo.sleepCountdownMs = 30000;
}
```

**报警期间不休眠：** `alarmFlag==1` 时重置倒计时，不进入休眠。

### 5.8 OLED 显示

**功能描述：** 500ms 刷新一次 OLED，显示以下内容：
1. GPS 状态（文字）
2. GPS 纬度数值（大字体）
3. GPS 经度数值（大字体）
4. 休眠倒计时（秒，格式 `Sleep: XXs`）

**显示布局（参考，以实际 OLED driver API 适配）：**

```
第1行（小字体）：GPS: 已定位 / 未定位
第2行（大字体标签+值）：Lat:
第3行（大字体）：23.1234（纬度值，保留4位小数）
第4行（大字体标签+值）：Lon:
第5行（大字体）：113.3456（经度值，保留4位小数）
第6行（小字体）：Sleep: 28s
```

**实现要点：**
- 使用 `DRIVER_OLED_Clear()`（或等效 API）清屏后重绘
- 数值部分使用大字体 API 显示（OLED driver 已提供大字体 API，以 .h 为准）
- 休眠倒计时显示 `user1TaskInfo.sleepCountdownMs / 1000` 秒
- 休眠状态下可关闭 OLED 显示（调用 OLED 关闭/清屏 API），唤醒后重新显示

---

## 六、driver 层修改需求

**位移计算辅助函数** 使用标准数学库 `math.h`（`cosf`、`sqrtf`），写在 `task_user1.c` 私有区域，不需要修改 driver 层。

**如以下情况出现，允许修改对应 driver：**

| 情况 | 允许修改的 driver | 修改内容 |
|------|-----------------|---------|
| GPS driver 仅提供原始 NMEA 字符串，不提供 float 经纬度 | GPS driver | 新增 `float latitude / longitude` 字段和 NMEA 解析逻辑 |
| IMU driver 不提供加速度数值读取 API | IMU driver | 新增加速度数据字段和读取 API |
| OLED driver 不支持大字体显示 | OLED driver | 新增大字体显示函数 |

> 修改 driver 时必须遵守 driver 层接口规范：命名用 `DRIVER_XXX_` 前缀，公有定义写 `.h`，私有定义写 `.c`，并在 `implementation_report.md` 的 driver 修改记录中标注。

---

## 七、编码约束

| 约束项 | 规则 |
|--------|------|
| 内存分配 | 禁止 `malloc`，全部静态分配 |
| 注释语言 | 中文 |
| 私有定义 | 写在 `.c` 文件中（`static` 修饰） |
| 公有定义 | 写在 `.h` 文件中 |
| 命名风格 | task层：小驼峰（`user1TaskInit`）；结构体：`xxxTaskInfo_t`；宏：`MODULE_CONSTANT` |
| 额外功能 | 不实现需求未提及的功能 |
| 分层约束 | task层可调用 stdlib层、driver层、func层，禁止直接调用 HAL |

---

## 八、输出物要求

Claude Code 完成开发后，必须输出以下文件：

### 1. 代码文件

- `task_user1.c`
- `task_user1.h`
- 如修改了 driver 层，输出修改后的 driver 文件，并用注释 `/* [MODIFIED] 原因 */` 标注修改点

### 2. 功能实现文档（implementation_report.md）

#### （a）功能实现状态清单

| 序号 | 功能描述 | 状态 | 说明 |
|------|---------|------|------|
| 1 | GPS 数据读取与上报（1s 周期）| ✅/⚠️/❌ | |
| 2 | 位移监测防盗（>10m 报警）| ✅/⚠️/❌ | |
| 3 | 震动检测防盗（加速度融合）| ✅/⚠️/❌ | |
| 4 | 声光报警（蜂鸣器+LED）| ✅/⚠️/❌ | |
| 5 | APP 远程防盗开关控制 | ✅/⚠️/❌ | |
| 6 | APP 远程报警消除 | ✅/⚠️/❌ | |
| 7 | APP TX 状态数据上报 | ✅/⚠️/❌ | |
| 8 | 休眠倒计时（30s）与 KEY 唤醒 | ✅/⚠️/❌ | |
| 9 | OLED 显示（GPS状态/坐标/倒计时）| ✅/⚠️/❌ | |

状态分三级：✅ 已实现、⚠️ 部分实现（说明缺什么）、❌ 未实现（说明原因）

#### （b）driver 层修改记录（如有）

| 文件 | 修改类型 | 修改内容 | 原因 |
|------|---------|---------|------|

#### （c）已实现功能的测试步骤

对每个已实现功能，给出具体测试方法和预期结果，例如：

- **GPS 上报测试**：上电后将设备置于室外，使用串口调试助手或 APP 观察 TX[4]、TX[5] 的经纬度数值是否每秒更新，TX[6] 是否变为 1（有效）。
- **震动报警测试**：开启防盗（APP 发送 RX[2]=1），轻拍设备，观察 APP 是否收到 TX[7]=1（报警），蜂鸣器和 LED 是否响应。
- **位移报警测试**：开启防盗并等待 GPS 定位，移动设备超过 10m，观察是否触发报警。
- **休眠测试**：上电后 30s 内不操作，观察 OLED 是否关闭，按任意 KEY 后是否唤醒并重置倒计时。

---

## 九、验证清单（开发完成后 Claude Code 逐项自查）

- [ ] `func_appcom.c / func_appcom.h` 未做任何修改
- [ ] 代码中未出现 `0x17`、`0x18`、`0x19`、`0x1A`、`0x21`、`0x22`、`0x23`、`0x24` 等 APP 端帧号
- [ ] `remoteVar_TX` 未被整体清零，只写了需要的索引
- [ ] 发给 APP 的变量全部使用 TX[4]~[15]，未使用 TX[0]~[3]
- [ ] `remoteVar_TX` 和 `remoteVar_RX` 索引均在 [0]~[15] 范围内，无越界
- [ ] RX 数据读取时未做额外字节序翻转（`func_appcom` 已完成翻转）
- [ ] TX 数据写入时未做任何字节序处理（直接赋值）
- [ ] `task_system.c / task_system.h` 未修改
- [ ] `userLib/` 下文件未修改
- [ ] `Core/` 下文件未修改
- [ ] 无 `malloc` 调用，全部静态分配
- [ ] 所有注释为中文
- [ ] 未实现需求未提及的额外功能
