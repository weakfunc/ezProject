# STM32 软件需求文档 — 红外非接触测温报警仪

> **本文档直接发给 STM32 项目目录下的 Claude Code 执行开发。**

---

## 一、项目概述

基于 STM32F103C8T6 的非接触式红外测温报警仪。系统通过红外温度传感器（I2C接口，如 MLX90614）周期性采集被测物体表面温度，通过 OLED 显示屏展示当前温度、报警阈值及电池电量图标。用户通过三个按键操作：KEY1 刷新 OLED 显示当前最新温度，KEY2 上调报警阈值（+0.1°C），KEY3 下调报警阈值（-0.1°C）。当测量温度超出设定阈值范围时，蜂鸣器和 RGB LED（红色）同时报警。系统使用 ADC1 采集电池电压并换算为电量百分比显示在 OLED 上。本项目**无 APP 通信需求**。

核心功能：
- 非接触红外测温（周期采集，精度 0.1°C）
- OLED 显示（温度、阈值、电量图标）
- 三键交互（刷新显示 / 阈值+0.1 / 阈值-0.1）
- 超阈值声光报警（蜂鸣器 + RGB红色LED）
- ADC 电池电量检测与 OLED 图标显示
- 测量完成后温度值在 OLED 至少保持显示 10 秒
- 30 秒无操作后自动进入低功耗休眠，任意KEY按键唤醒休眠

---

## 二、开发前置步骤

**Claude Code 开发前必须执行以下步骤，不可跳过：**

1. 浏览 `MDK-ARM/userDriver/` 目录，列出所有保留的 `.c/.h` 文件
2. 逐个阅读每个 driver 的 `.h` 文件，**只阅读「向上提供」部分**（公有结构体、Init/Update/功能函数）
3. 将所有可用 driver 的结构体和 API 汇总记录
4. 根据汇总结果确认以下功能对应的 driver 是否存在：
   - 红外温度传感器（I2C，如 MLX90614 或类似）
   - OLED 显示（SSD1306 或类似）
   - 按键输入（KEY）
   - 蜂鸣器（BEEP/BUZZER）
   - RGB LED
   - ADC（电压采集）
5. 以上述 API 清单为基础进行后续 task 层设计，**不得假设不存在的 API**

---

## 二（附）、禁止修改的文件清单

以下文件**严禁修改**，违反将导致系统异常：

| 文件 | 原因 |
|------|------|
| `func_appcom.c` / `func_appcom.h` | 帧 CMD 编号（TX: 0x09~0x0C, RX: 0x13~0x16）和帧数量（4TX+4RX）是框架与 ESP32 固件的固定协议，修改会导致 BLE 断连 |
| `task_system.c` / `task_system.h` | 系统任务，框架已实现，不可改动 |
| `userLib/` 下所有文件 | stdlib 层，框架已实现 |
| `Core/` 下所有文件 | HAL 层，CubeMX 生成，不可修改 |

**本项目无 APP 通信需求，不使用 remoteVar_TX/RX，不涉及 appcom 操作。**

额外注意事项：
- 代码中**不得出现** `0x17~0x1A`、`0x21~0x24` 等 APP 端帧号
- **不得整体清零** `remoteVar_TX`
- **不得调用 HAL 层函数**，task 层只能调用 driver/stdlib/func 层 API

---

## 三、task 层设计

### 1. 文件规划

只使用框架默认的 `task_user1.c / task_user1.h`，在其中实现全部应用逻辑。

### 2. 公有结构体设计

```c
// task_user1.h
typedef struct {
    uint32_t taskCnt;           // 任务计数器，用于取余获得子周期

    /* 温度相关 */
    float    tempLatest;        // 周期采集到的最新温度值（°C）
    float    tempDisplay;       // OLED 当前显示的温度值（按KEY1时更新）
    float    alarmThreshHigh;   // 报警上限阈值（°C），初始值 37.5
    float    alarmThreshLow;    // 报警下限阈值（°C），初始值 35.0（可选，若需求只用上限则只保留上限）

    /* 按键相关 */
    uint8_t  key1Pressed;       // KEY1 触发标志（边沿检测后置1，处理后清0）
    uint8_t  key2Pressed;       // KEY2 触发标志
    uint8_t  key3Pressed;       // KEY3 触发标志

    /* 报警相关 */
    uint8_t  alarmActive;       // 当前是否处于报警状态（1=是，0=否）

    /* 电池相关 */
    uint8_t  battPercent;       // 电池电量百分比（0~100）

    /* 显示保持计时 */
    uint32_t displayHoldCnt;    // 按KEY1后的显示保持计数（2ms计，>=5000时满10秒）

    /* 休眠计时 */
    uint32_t idleCnt;           // 无操作计时（2ms计，>=15000时满30秒）
    uint8_t  sleepFlag;         // 是否已进入休眠（1=休眠中）

} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;
void user1TaskInit(void);
void user1TaskUpdata(void *argument);
```

> **注意**：仅在 `.h` 中放结构体定义和 extern 声明，具体实现放 `.c`。结构体字段含义中文注释。

### 3. Init 函数设计

`user1TaskInit()` 中依次执行：

1. 调用红外温度传感器 driver 的 `DRIVER_XXX_Init()`（根据实际 driver 命名）
2. 调用 OLED driver 的 `DRIVER_XXX_Init()`
3. 调用按键 driver 的 `DRIVER_XXX_Init()`（若需要）
4. 调用蜂鸣器 driver 的 `DRIVER_XXX_Init()`（若需要）
5. 调用 RGB LED driver 的 `DRIVER_XXX_Init()`（若需要）
6. 调用 ADC driver 的 `DRIVER_XXX_Init()`（若需要）
7. 初始化 `user1TaskInfo` 各字段：
   - `alarmThreshHigh = 37.5f`（初始上限，可根据实际需求调整，题目说人体测温范围 35~42°C，报警上限默认 37.5°C）
   - `alarmThreshLow = 35.0f`（初始下限，低于此值也报警；如需求只用上限则去掉）
   - `tempDisplay = 0.0f`、`tempLatest = 0.0f`
   - 其余计数器归零

### 4. Updata 循环设计

基础周期 2ms，通过 `taskCnt` 取余实现多级子周期：

```
taskCnt % 50  == 0  →  100ms 周期
taskCnt % 250 == 0  →  500ms 周期
taskCnt % 500 == 0  →  1s 周期
```

**循环结构如下（伪代码）：**

```c
void user1TaskUpdata(void *argument) {
    user1TaskInit();
    for(;;) {
        user1TaskInfo.taskCnt++;

        /* ① 每 100ms：温度采集、ADC 采集、报警判断 */
        if(user1TaskInfo.taskCnt % 50 == 0) {
            // 调用红外 driver Update 更新温度
            // 读取最新温度到 tempLatest
            // 调用 ADC driver 更新，读取电池电量换算为 battPercent
            // 执行报警判断逻辑
            // 执行蜂鸣器/RGB 控制
        }

        /* ② 每 2ms（每轮）：按键扫描，边沿检测 */
        {
            // 调用按键 driver Update（若该 driver 需要周期调用）
            // 检测 KEY1/2/3 下降沿，置对应 keyXPressed 标志
        }

        /* ③ 按键事件处理（每轮检查标志） */
        {
            // KEY1：tempDisplay = tempLatest; displayHoldCnt = 0; 刷新 OLED; 重置 idleCnt
            // KEY2：alarmThreshHigh += 0.1f; 限幅到 60.0f; 刷新 OLED; 重置 idleCnt
            // KEY3：alarmThreshHigh -= 0.1f; 限幅到 0.0f; 刷新 OLED; 重置 idleCnt
            // 任意按键按下后清 sleepFlag，唤醒系统
        }

        /* ④ 休眠计时（每轮） */
        {
            // 若 sleepFlag == 0：idleCnt++
            // 若 idleCnt >= 15000（30s）：进入低功耗休眠
        }

        /* ⑤ 显示保持计时（每轮） */
        {
            // 若 displayHoldCnt < 5000（10s）：displayHoldCnt++
            // displayHoldCnt 满后 OLED 不主动清屏，值保持即可（显示内容不变）
        }

        osDelay(2);
    }
}
```

---

## 四、APP 通信变量布局

**本项目无 APP 通信，不使用 remoteVar_TX/RX，跳过本节。**

---

## 五、功能详细需求

### 5.1 红外温度采集（周期后台更新）

**功能描述：** 每 100ms 调用一次红外温度传感器 driver，更新 `tempLatest`。读取 API 在后台周期执行，与 KEY1 显示刷新解耦。

**实现逻辑：**
- 每 100ms（`taskCnt % 50 == 0`）调用温度传感器 driver 的 Update 函数
- 读取 driver 公有结构体中的温度值存入 `user1TaskInfo.tempLatest`
- 应增加有效性校验：若返回值超出 -20°C ~ 120°C 范围，视为无效，保持上一次有效值

**涉及 driver API（根据实际 driver 命名调整）：**
```c
DRIVER_INFRARED_Update();                         // 周期更新
float t = infraredInfo.temperature;               // 读取温度（字段名以实际 .h 为准）
```

**关键参数：**
- 采集周期：100ms
- 有效范围：-20°C ~ 120°C
- 人体测温精度要求：±0.2°C（driver 层硬件精度，task 层无需处理）

---

### 5.2 KEY1 — 刷新 OLED 显示

**功能描述：** 用户按下 KEY1，将后台最新温度 `tempLatest` 更新到 `tempDisplay`，立即刷新 OLED 画面，并重置显示保持计时。未按时 OLED 维持上一次显示值。

**实现逻辑（边沿检测，防抖由 driver 负责）：**
```
检测到 KEY1 下降沿：
    user1TaskInfo.tempDisplay  = user1TaskInfo.tempLatest
    user1TaskInfo.displayHoldCnt = 0           // 重置保持计时
    user1TaskInfo.idleCnt = 0                  // 重置休眠计时
    调用 OLED 刷新函数（见 5.5）
```

**注意：** OLED 在未按 KEY1 时**不主动更新**温度数值区域，维持上一次显示内容。

---

### 5.3 KEY2 / KEY3 — 调整报警阈值

**功能描述：** KEY2 将报警上限阈值 +0.1°C，KEY3 将其 -0.1°C，步进 0.1°C，调整后立即刷新 OLED 阈值显示区域。

**实现逻辑：**
```
KEY2 下降沿：
    alarmThreshHigh += 0.1f
    限幅：alarmThreshHigh = MIN(alarmThreshHigh, 60.0f)
    idleCnt = 0
    刷新 OLED

KEY3 下降沿：
    alarmThreshHigh -= 0.1f
    限幅：alarmThreshHigh = MAX(alarmThreshHigh, 0.0f)
    idleCnt = 0
    刷新 OLED
```

**注意：** 浮点加减累积误差处理——若 driver 层提供整型方式，可用 `int16_t alarmThreshHighX10`（单位 0.1°C）在整数域运算，显示时除以 10.0f。**优先使用整数域避免浮点累积误差**，具体视实现便利决定。

**关键参数：**
- 步进：0.1°C
- 上限最大值：60.0°C
- 上限最小值：0.0°C（不允许为负，防止误操作）
- 调整范围提示：题目要求用户可设定报警阈值用于 32~45°C 范围，但代码不限制用户只能设在此区间

---

### 5.4 报警判断与声光控制

**功能描述：** 每 100ms 判断 `tempLatest` 是否超出报警范围，若超出则蜂鸣器鸣叫、RGB LED 亮红色；否则停止报警。

**实现逻辑：**
```
每 100ms：
    if (tempLatest > alarmThreshHigh || tempLatest < alarmThreshLow) {
        alarmActive = 1
        蜂鸣器：开启（鸣叫）
        RGB LED：亮红色（R=1, G=0, B=0）
    } else {
        alarmActive = 0
        蜂鸣器：关闭
        RGB LED：关闭（全0）
    }
```

**若需求只设上限（题目描述侧重超温报警）**，去掉下限判断，只保留 `tempLatest > alarmThreshHigh`。根据题目原文"当测量温度超出设定范围时报警"，建议**同时实现上下限报警**，`alarmThreshLow` 初始值 35.0°C（人体测温下限）。

**涉及 driver API（根据实际 driver 命名调整）：**
```c
DRIVER_BEEP_SetState(1);    // 蜂鸣器开（或等效函数）
DRIVER_BEEP_SetState(0);    // 蜂鸣器关
DRIVER_RGB_SetColor(255, 0, 0);   // 红色报警
DRIVER_RGB_SetColor(0, 0, 0);     // 关闭
```

---

### 5.5 OLED 显示

**功能描述：** OLED 显示四类信息：当前温度、报警阈值、报警状态提示、电池电量图标。

**显示布局（示意，根据 OLED driver 分辨率适配）：**

```
┌─────────────────────┐
│ Temp: xx.x C        │  ← 当前显示温度（KEY1刷新，初始显示 0.0 或 --.-）
│ Alarm: xx.x C       │  ← 当前报警上限阈值（KEY2/3调整后实时更新）
│ [ALARM!]            │  ← 仅在 alarmActive=1 时显示，正常时空白
│ [BATT: 电量图标 XX%]│  ← 电池百分比，每 500ms 更新
└─────────────────────┘
```

**电量图标绘制（文字版，若 driver 不支持像素绘图）：**
- 100%~75%：`[====]`
- 74%~50%：`[=== ]`
- 49%~25%：`[==  ]`
- 24%~10%：`[=   ]`
- 9%~0%：`[    ]`（闪烁警示，每 500ms 翻转，借助 taskCnt % 250 实现）

**刷新策略：**
- 温度区域：仅在 KEY1 按下时刷新（调用一次 OLED 全局刷新或局部刷新）
- 阈值区域：KEY2/KEY3 按下时刷新
- 电量图标：每 500ms（`taskCnt % 250 == 0`）刷新
- 报警提示：每 100ms 随报警判断一起刷新（或仅在状态变化时刷新）

**涉及 driver API（根据实际 driver 命名调整）：**
```c
DRIVER_OLED_Clear();
DRIVER_OLED_ShowString(x, y, str);
DRIVER_OLED_ShowFloat(x, y, val, 1);  // 1位小数
DRIVER_OLED_Refresh();                // 若为缓冲显示需手动刷新
```

---

### 5.6 ADC 电池电量检测

**功能描述：** 使用 ADC1 采集电池电压，换算为 0~100% 的电量百分比，显示在 OLED。

**实现逻辑：**
- 每 500ms（`taskCnt % 250 == 0`）采集一次 ADC
- 读取 ADC 原始值，换算为电压：`voltage = raw * 3.3f / 4096.0f`（12位ADC，参考电压3.3V）
- 若电池通过分压电阻接入（例如分压比 1/2），则：`battVoltage = voltage * 2.0f`
- 电量百分比换算（以 3.7V 锂电池为例）：
  ```
  battVoltage >= 4.2V → 100%
  battVoltage <= 3.3V → 0%
  线性插值：percent = (battVoltage - 3.3f) / (4.2f - 3.3f) * 100
  限幅到 0~100
  ```
  > **注意：** 实际分压比和电池规格请根据硬件原理图确认，上述为常见方案，需在代码注释中标注实际参数。
- 换算结果存入 `user1TaskInfo.battPercent`（uint8_t，0~100）
- 刷新 OLED 电量图标

**涉及 driver API（根据实际 driver 命名调整）：**
```c
DRIVER_ADC_Update();                          // 触发采集（若 driver 需要）
uint32_t raw = adcInfo.channel[0].value;      // 读取原始值（字段名以实际 .h 为准）
```

---

### 5.7 显示保持（10秒）

**功能描述：** 按下 KEY1 后温度值在 OLED 上保持显示至少 10 秒，期间 OLED 不清除温度内容。

**实现逻辑：**
- 按下 KEY1 时：`displayHoldCnt = 0`
- 每 2ms：`if (displayHoldCnt < 5000) displayHoldCnt++`
- 只要 `displayHoldCnt < 5000`，OLED 温度区域维持显示 `tempDisplay` 的值
- 10 秒后（`displayHoldCnt >= 5000`）OLED 继续显示，不主动清屏，保持最后的值（无需额外操作）

> 题目要求"温度值能在屏幕上保持显示至少 10 秒"，实现方式为 KEY1 触发后不主动清屏，自然满足。

---

### 5.8 30秒无操作自动休眠

**功能描述：** 系统在无任何按键操作 30 秒后自动进入低功耗休眠（STM32 STOP 模式或 SLEEP 模式）。

**实现逻辑：**
- 每 2ms：若 `sleepFlag == 0`，则 `idleCnt++`
- 任意按键按下时：`idleCnt = 0`，若 `sleepFlag == 1` 则唤醒系统（清 `sleepFlag`，重新初始化外设）
- 当 `idleCnt >= 15000`（30秒）：
  ```
  sleepFlag = 1
  关闭蜂鸣器、RGB LED
  OLED 清屏或关闭
  进入低功耗模式（调用 HAL_PWR_EnterSLEEPMode 或 STOP 模式）
  ```

**重要约束：** task 层不得直接调用 HAL_PWR_xxx，若 driver 层没有封装低功耗 API，需要在 **driver 层新增**低功耗封装（见第六节）。

---

## 六、driver 层修改需求（如需要）

根据开发前置步骤汇总的 driver API，若以下功能缺少对应封装，需在 driver 层新增（不得在 task 层直接调用 HAL）：

### 6.1 低功耗进入/唤醒封装（如无现成 driver）

若框架中无低功耗 driver，新增 `driver_power.c / driver_power.h`：

```c
// driver_power.h（向上提供部分）
void DRIVER_POWER_EnterSleep(void);   // 进入低功耗模式
void DRIVER_POWER_WakeUp(void);       // 唤醒后恢复外设
```

实现中可调用 `HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI)`，唤醒源为按键外部中断。

### 6.2 蜂鸣器控制（如无现成 driver）

若框架无蜂鸣器 driver，新增 `driver_beep.c / driver_beep.h`：

```c
void DRIVER_BEEP_Init(void);
void DRIVER_BEEP_SetState(uint8_t state);  // 1=开, 0=关
```

### 6.3 RGB LED 控制（如无现成 driver）

若框架无 RGB LED driver，新增 `driver_rgb.c / driver_rgb.h`：

```c
void DRIVER_RGB_Init(void);
void DRIVER_RGB_SetColor(uint8_t r, uint8_t g, uint8_t b);  // 0或1（数字IO）/ 0~255（PWM）
```

> **所有新增 driver 文件必须遵守 driver 层接口规范：**
> - 提供公有信息结构体 `xxxInfo`（若有状态需要暴露）
> - 命名风格：`DRIVER_XXX_FunctionName`
> - 私有定义写 `.c`，公有定义写 `.h`
> - 中文注释
> - 禁止 malloc

---

## 七、输出物要求

Claude Code 完成开发后，**必须输出以下文件**：

### 1. 代码文件

- `userApp/task_user1.c`
- `userApp/task_user1.h`
- 若新增/修改了 driver 层文件，一并输出，并在文件顶部注释中标注：
  ```c
  /* [新增] driver_beep.c — 蜂鸣器驱动，因框架原driver中无此模块，本次新增 */
  /* [修改] driver_adc.c — 新增 DRIVER_ADC_GetVoltage() 函数，原有函数不变 */
  ```

### 2. 功能实现文档（`implementation_report.md`）

#### (a) 功能实现状态清单

| 序号 | 功能描述 | 状态 | 说明 |
|------|----------|------|------|
| 1 | 红外温度周期采集（100ms） | ✅/⚠️/❌ | |
| 2 | KEY1 刷新 OLED 温度显示 | ✅/⚠️/❌ | |
| 3 | KEY2 阈值+0.1°C | ✅/⚠️/❌ | |
| 4 | KEY3 阈值-0.1°C | ✅/⚠️/❌ | |
| 5 | 超阈值蜂鸣器报警 | ✅/⚠️/❌ | |
| 6 | 超阈值 RGB 红色报警 | ✅/⚠️/❌ | |
| 7 | OLED 显示温度/阈值/报警提示 | ✅/⚠️/❌ | |
| 8 | OLED 显示电池电量图标 | ✅/⚠️/❌ | |
| 9 | ADC1 电池电压采集换算百分比 | ✅/⚠️/❌ | |
| 10 | 显示保持 10 秒 | ✅/⚠️/❌ | |
| 11 | 30 秒无操作自动休眠 | ✅/⚠️/❌ | |

#### (b) driver 层修改记录（如有修改）

| 文件 | 修改类型 | 修改内容 | 原因 |
|------|----------|----------|------|
| | | | |

#### (c) 已实现功能的测试步骤

逐功能给出**具体测试方法和预期结果**，例如：

1. **温度采集验证**：用手握住传感器，等待 2 秒，按 KEY1，OLED 应显示约 33~36°C 的体表温度。
2. **KEY2/KEY3 阈值调整**：连按 KEY2 三次，OLED 阈值显示应从初始值变化 +0.3°C；连按 KEY3 三次应回到初始值。
3. **报警触发验证**：将阈值调低至当前温度以下（连按 KEY3），应听到蜂鸣器鸣叫，RGB LED 亮红色；将阈值调回正常范围，报警应停止。
4. **电量图标验证**：上电后 OLED 电量区域应显示当前电量百分比及图标，图标格数随电压变化。
5. **休眠验证**：上电后静置 30 秒不操作，系统应进入低功耗状态（OLED 熄灭/清屏），按任意键应唤醒。
6. **显示保持验证**：按 KEY1 刷新后，温度显示应至少维持 10 秒不消失。

---

## 八、编码约束（Claude Code 必须遵守）

| 约束项 | 规则 |
|--------|------|
| 内存分配 | 禁止 `malloc`，全部静态分配 |
| 注释语言 | 中文 |
| 私有定义 | 写在 `.c` 文件中 |
| 公有定义 | 写在 `.h` 文件中 |
| 命名风格 | task层：小驼峰（`user1TaskInit`）；结构体：`xxxTaskInfo_t`；宏：`MODULE_CONSTANT` |
| 额外功能 | **不实现需求未提及的功能** |
| HAL 调用 | task 层禁止直接调用 HAL 函数 |
| 层级约束 | task 层只调用 stdlib/driver/func 层 API |
| appcom | 本项目无 APP 通信，不使用 `remoteVar_TX/RX`，不调用 appcom 相关函数 |

---

## 九、验证清单（开发完成后自查）

Claude Code 提交代码前，必须逐项确认：

- [ ] `func_appcom.c/h` 未被修改
- [ ] `task_system.c/h` 未被修改
- [ ] `userLib/` 下文件均未修改
- [ ] `Core/` 下文件均未修改
- [ ] 代码中不出现 `0x17~0x1A`、`0x21~0x24` 等 APP 端帧号
- [ ] 代码中不出现 `remoteVar_TX` 或 `remoteVar_RX` 的赋值操作
- [ ] task 层未直接调用任何 `HAL_` 开头的函数
- [ ] 所有变量静态分配，无 `malloc`
- [ ] 注释为中文
- [ ] 新增 driver 文件遵守 driver 层接口规范
- [ ] `user1TaskInfo_t` 中无嵌套 struct（防止内存对齐问题，与 remoteVar_t union 规则同理）
