# STM32 软件需求文档
# 智能控温足浴桶控制系统

---

## 一、项目概述

本项目为智能控温足浴桶嵌入式控制系统，基于 STM32F103C8T6，运行于 HAL + FreeRTOS CMSIS_V2 框架之上。

核心功能：
- **智能控温**：PID 闭环控制水温，目标温度范围 35~48℃，精度 ±0.5℃；超温保护（≥50℃自动断电/停止加热）；防干烧保护（水位检测，无水时禁止加热）
- **多模式按摩**：驱动直流电机开环 PWM 控制，支持轻柔档（60~80rpm）、标准档（100~120rpm）、强劲档（140~160rpm）三挡，以及停止状态
- **定时功能**：APP 下发定时时长（分钟），倒计时结束后自动停机
- **APP 远程控制**：通过 BLE（ESP32 桥接）接收 APP 指令，上报实时状态
- **安全保护**：超温保护、防干烧保护（硬件水位检测信号输入），故障状态实时上报

无板载硬件按键，所有用户控制输入来自 APP。

---

## 二、开发前置步骤

**请 CODEX在开始编写任何代码之前，按以下步骤操作：**

1. 浏览 `MDK-ARM/userDriver/` 目录，列出所有 `.c/.h` 文件
2. 逐个阅读每个 driver 的 `.h` 文件中「向上提供」部分（公有结构体 `xxxInfo` 和所有 `DRIVER_XXX_*` 函数）
3. 将可用的结构体和 API 汇总列表，确认以下 driver 是否存在：
   - 温度传感器 driver（NTC 热敏电阻或 DS18B20 等）
   - PWM 输出 driver（电机控制）
   - GPIO 输入 driver 或直接 HAL GPIO（水位检测干节点输入）
   - appcom func（`userFunc/func_appcom.h`）
4. 根据实际存在的 driver API 进行后续开发，若 API 不足则按第六节说明修改 driver 层

---

## 二（附）、禁止修改的文件清单

以下文件**严禁修改**：

| 文件 | 禁止原因 |
|------|----------|
| `func_appcom.c` / `func_appcom.h` | 帧 CMD 编号（TX: 0x09~0x0C，RX: 0x13~0x16）和帧数量（4TX+4RX）是框架与 ESP32 固件的固定约定，任何修改都会导致 BLE 断连 |
| `task_system.c` / `task_system.h` | 系统任务，框架已实现，负责 stdlib 状态机维护和 appcom 通信更新 |
| `userLib/` 下所有文件 | stdlib 层，框架已实现，不可修改 |
| `Core/` 下所有文件 | HAL 层，CubeMX 生成，不可修改 |

**必须遵守的注意事项：**

- `TX[0]~[3]`（帧 0x09）**不会到达 APP**，发送给 APP 的变量必须使用 `TX[4]~[15]`
- **不要整体清零** `remoteVar_TX`，只写需要的索引，未使用的保持原值
- **不要在代码中出现** `0x17~0x1A`、`0x21~0x24` 等 APP 端帧号，STM32 端不需要知道这些编号
- task 层读写 remoteVar 时：读 RX 直接用 `var_float`/`var_uint32`，禁止再做字节序翻转；写 TX 直接赋值，禁止做任何字节序处理

---

## 三、task 层设计

### 1. 文件规划

仅使用默认的应用任务文件：

| 文件 | 职责 |
|------|------|
| `task_user1.c` / `task_user1.h` | 全部应用逻辑：温控 PID、电机控制、定时管理、安全保护、APP 通信读写 |

### 2. 公有结构体设计

```c
// task_user1.h
typedef struct {
  uint32_t taskCnt;           // 任务计数，用于子周期划分

  /* === 温控相关 === */
  float    currentTemp;       // 当前水温（℃），由温度传感器读取
  float    targetTemp;        // 目标水温（℃），APP 设定，范围 35~48
  float    pidIntegral;       // PID 积分项
  float    pidLastError;      // PID 上次误差（用于微分项）
  uint8_t  heaterOn;          // 加热器状态：0=关，1=开
  uint8_t  overTempFlag;      // 超温标志：0=正常，1=超温
  uint8_t  dryBurnFlag;       // 防干烧标志：0=有水，1=缺水

  /* === 按摩电机相关 === */
  uint8_t  massageMode;       // 当前按摩模式：0=停止,1=轻柔,2=标准,3=强劲
  uint8_t  massageModeTarget; // APP 下发的目标模式（待应用）
  uint32_t motorPwmDuty;      // 当前电机 PWM 占空比（0~100）

  /* === 定时相关 === */
  uint32_t timerSetSec;       // 定时总时长（秒），0=不定时
  uint32_t timerRemainSec;    // 剩余时间（秒）
  uint8_t  timerActive;       // 定时是否激活：0=无，1=倒计时中

  /* === 系统状态 === */
  uint8_t  sysRunning;        // 系统运行状态：0=待机，1=运行中
  uint8_t  faultCode;         // 故障码：0=正常，1=超温，2=干烧，3=超温+干烧

} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;
void user1TaskInit(void);
void user1TaskUpdata(void *argument);
```

### 3. Init 函数设计

```c
void user1TaskInit(void) {
  /* 初始化温度传感器 driver */
  DRIVER_TEMP_Init();       // 根据实际 driver 名称调用

  /* 初始化 PWM 输出（电机） */
  DRIVER_PWM_Init();        // 根据实际 driver 名称调用

  /* 初始化水位检测 GPIO（若有专用 driver） */
  // DRIVER_WATER_Init();   // 若水位检测直接读 HAL_GPIO，则无需此调用

  /* 初始化状态变量 */
  user1TaskInfo.targetTemp        = 40.0f;   // 默认目标温度 40℃
  user1TaskInfo.massageMode       = 0;        // 默认停止
  user1TaskInfo.massageModeTarget = 0;
  user1TaskInfo.sysRunning        = 0;        // 默认待机
  user1TaskInfo.timerSetSec       = 0;
  user1TaskInfo.timerRemainSec    = 0;
  user1TaskInfo.timerActive       = 0;
  user1TaskInfo.pidIntegral       = 0.0f;
  user1TaskInfo.pidLastError      = 0.0f;
  user1TaskInfo.heaterOn          = 0;
  user1TaskInfo.overTempFlag      = 0;
  user1TaskInfo.dryBurnFlag       = 0;
  user1TaskInfo.faultCode         = 0;
  user1TaskInfo.motorPwmDuty      = 0;
}
```

### 4. Updata 循环设计

基础周期 2ms，各功能执行频率规划：

| 子周期 | taskCnt 取余条件 | 执行内容 |
|--------|-----------------|----------|
| 2ms | 每次循环 | `DRIVER_TEMP_Update()`（若需周期调用）；读取水位检测 GPIO |
| 10ms | `% 5 == 0` | PID 温控计算，输出加热器控制 |
| 100ms | `% 50 == 0` | 读取 APP RX 指令（目标温度、模式、开关机、定时）；将状态写入 TX；更新故障码 |
| 1000ms | `% 500 == 0` | 定时倒计时递减（每秒减 1）；定时到期处理 |

---

## 四、APP 通信变量布局

### TX（STM32 → APP，仅 [4]~[15] 对 APP 可见）

| 索引 | 有效宽度 | 变量含义 | 数据类型 | 备注 |
|------|----------|----------|----------|------|
| [4] | 4B | 当前水温（℃，保留1位小数） | float | 如 38.5 |
| [5] | 4B | 目标水温（℃，整数） | uint32 | 35~48 |
| [6] | 1B | 当前按摩模式 | uint32低8位 | 0=停止,1=轻柔,2=标准,3=强劲 |
| [7] | 1B | 系统状态字节 | uint32低8位 | bit0=运行中,bit1=加热中,bit2=干烧告警,bit3=超温告警 |
| [8] | 4B | 剩余定时时间（秒） | uint32 | 0=无定时 |
| [9] | 4B | 电机 PWM 占空比（调试） | uint32 | 0~100 |
| [10] | 1B | 故障码 | uint32低8位 | 0=正常,1=超温,2=干烧 |
| [11] | 1B | 保留 | — | 保持原值，不写 |
| [12]~[15] | — | 保留 | — | 保持原值，不写 |

**TX[0]~[3] 说明：** 帧 0x09 被 ESP32 截留，不转发 APP，本项目不使用这4个槽位传递数据。

### RX（APP → STM32，全部可用）

| 索引 | 有效宽度 | 变量含义 | 数据类型 | 备注 |
|------|----------|----------|----------|------|
| [0] | 4B | 设定目标温度（℃，整数） | uint32 | 35~48，APP 直接发整数 |
| [1] | 4B | 定时时长（分钟） | uint32 | 0=不定时，最大 60 |
| [2] | 1B | 按摩模式指令 | uint32低8位 | 0=停止,1=轻柔,2=标准,3=强劲 |
| [3] | 1B | 系统开关指令 | uint32低8位 | 0=待机,1=运行 |
| [4]~[15] | — | 保留（不使用） | — | — |

**读取代码示例：**
```c
/* 100ms 周期内读取 APP 下发数据 */
uint32_t appTarget = remoteInfo.remoteVar_RX[0].var_uint32; // 目标温度
uint32_t appTimer  = remoteInfo.remoteVar_RX[1].var_uint32; // 定时分钟
uint8_t  appMode   = (uint8_t)(remoteInfo.remoteVar_RX[2].var_uint32 & 0xFF); // 按摩模式
uint8_t  appPower  = (uint8_t)(remoteInfo.remoteVar_RX[3].var_uint32 & 0xFF); // 开关

/* 写入 TX 上报状态 */
remoteInfo.remoteVar_TX[4].var_float   = user1TaskInfo.currentTemp;
remoteInfo.remoteVar_TX[5].var_uint32  = (uint32_t)user1TaskInfo.targetTemp;
remoteInfo.remoteVar_TX[6].var_uint32  = (uint32_t)user1TaskInfo.massageMode;
remoteInfo.remoteVar_TX[7].var_uint32  = (uint32_t)statusByte;
remoteInfo.remoteVar_TX[8].var_uint32  = user1TaskInfo.timerRemainSec;
remoteInfo.remoteVar_TX[9].var_uint32  = user1TaskInfo.motorPwmDuty;
remoteInfo.remoteVar_TX[10].var_uint32 = (uint32_t)user1TaskInfo.faultCode;
```

---

## 五、功能详细需求

### 5.1 系统开关机

**功能描述：** APP 发送开关机指令（RX[3]），STM32 切换 `sysRunning` 状态。

**实现逻辑：**
- `appPower == 1` 且当前为待机：进入运行状态，开始温控和按摩（按上次模式）
- `appPower == 0` 且当前为运行：进入待机状态，关闭加热器，电机停止，清零定时
- 待机状态下，加热 PWM 输出固定为 0，电机 PWM 输出固定为 0
- 开关机指令在 100ms 周期内读取，检测到变化时立即执行

**涉及 API：**
- 驱动加热器：通过 PWM driver 或 GPIO driver 控制加热继电器引脚
- 驱动电机：通过 PWM driver 设置占空比为 0

### 5.2 智能温控（PID 闭环）

**功能描述：** 以 10ms 为控制周期，读取当前水温，计算 PID 输出，控制加热器 PWM 或继电器开关。

**实现逻辑（位置式 PID，10ms 周期执行）：**

```
误差 error = targetTemp - currentTemp
积分项：pidIntegral += error × dt（dt = 0.01s）
微分项：derivative = (error - pidLastError) / dt
PID输出 = Kp×error + Ki×pidIntegral + Kd×derivative
限幅：输出 clamp 到 [0, 100]（PWM 占空比）
pidLastError = error
```

**PID 初始参数（可调）：**
- Kp = 10.0f
- Ki = 0.5f
- Kd = 1.0f
- 积分限幅：pidIntegral clamp 到 [-50, 50]，防积分饱和

**加热控制策略：**
- PID 输出 > 0 且系统运行中 → 输出对应 PWM 占空比到加热器（或采用 Bang-Bang + 积分：输出≥50则开继电器）
- 超温保护触发（currentTemp ≥ 50℃）→ 强制关闭加热器，设 heaterOn=0，overTempFlag=1
- 超温恢复：currentTemp < 47℃ 且无其他故障时，overTempFlag=0，恢复正常控温
- 防干烧（dryBurnFlag==1）→ 强制关闭加热器，不论 PID 输出

**系统状态字节 statusByte（TX[7]）组装：**
```c
uint8_t statusByte = 0;
if (user1TaskInfo.sysRunning)   statusByte |= (1 << 0);
if (user1TaskInfo.heaterOn)     statusByte |= (1 << 1);
if (user1TaskInfo.dryBurnFlag)  statusByte |= (1 << 2);
if (user1TaskInfo.overTempFlag) statusByte |= (1 << 3);
```

**涉及 API：**
- 读温度：`tempInfo.tempValue`（或对应结构体字段，以实际 driver 为准）
- 控加热：PWM driver 设置加热通道占空比，或 GPIO driver 控制继电器

### 5.3 多模式按摩（电机开环控制）

**功能描述：** 根据 APP 下发的按摩模式（RX[2]），设置电机 PWM 占空比，实现三档转速。

**模式与 PWM 占空比映射（以额定转速估算，可根据实际电机调整）：**

| 模式值 | 含义 | PWM 占空比 | 目标转速 |
|--------|------|-----------|----------|
| 0 | 停止 | 0% | 0 rpm |
| 1 | 轻柔 | 40% | ~70 rpm |
| 2 | 标准 | 60% | ~110 rpm |
| 3 | 强劲 | 80% | ~150 rpm |

**实现逻辑：**
- 100ms 周期内读取 `appMode`（RX[2]），与当前 `massageMode` 比较
- 发生变化时，更新 `massageMode` 并调用 PWM driver 设置对应占空比
- 系统待机（sysRunning==0）时，强制 PWM=0，忽略 APP 模式指令（但存储 massageModeTarget 供下次开机恢复）
- 将当前占空比写入 `motorPwmDuty`，上报 TX[9]（调试用）

**涉及 API：**
- PWM driver：`DRIVER_PWM_SetDuty(channel, duty)`（以实际 API 为准）

### 5.4 定时功能

**功能描述：** APP 下发定时时长（分钟，RX[1]），系统倒计时，时间到自动停机。

**实现逻辑：**
- 100ms 周期读取 `appTimer`（RX[1]）：
  - 若值 > 0 且与上次不同：设定 `timerSetSec = appTimer × 60`，`timerRemainSec = timerSetSec`，`timerActive = 1`
  - 若值 == 0：取消定时，`timerActive = 0`，`timerRemainSec = 0`
- 1s 周期倒计时：若 `timerActive == 1` 且 `sysRunning == 1`：
  - `timerRemainSec--`
  - 若 `timerRemainSec == 0`：触发自动停机（等同于 APP 发送 power=0），`timerActive = 0`
- 上报 TX[8] = `timerRemainSec`

**注意：** 定时仅在系统运行时倒计时，待机时暂停。

### 5.5 安全保护

**功能描述：** 超温保护和防干烧保护，硬件信号输入，软件逻辑响应。

**超温保护：**
- 每次温度读取后判断（2ms 或 10ms 周期）
- `currentTemp >= 50.0f`：overTempFlag=1，强制关闭加热器
- `currentTemp < 47.0f` 且 overTempFlag==1：恢复 overTempFlag=0

**防干烧保护：**
- 水位检测为数字 GPIO 输入（低电平=有水，高电平=缺水，以实际硬件为准）
- 2ms 周期读取水位 GPIO，更新 `dryBurnFlag`
- `dryBurnFlag == 1`：强制关闭加热器，设故障码
- 水位恢复正常后：dryBurnFlag=0，故障码清零，恢复正常控温

**故障码更新（100ms 周期）：**
```c
if (user1TaskInfo.dryBurnFlag && user1TaskInfo.overTempFlag)
  user1TaskInfo.faultCode = 3;
else if (user1TaskInfo.overTempFlag)
  user1TaskInfo.faultCode = 1;
else if (user1TaskInfo.dryBurnFlag)
  user1TaskInfo.faultCode = 2;
else
  user1TaskInfo.faultCode = 0;
```

### 5.6 APP 通信读写（100ms 周期）

**TX 写入（状态上报）：**
```c
uint8_t statusByte = 0;
if (user1TaskInfo.sysRunning)   statusByte |= (1 << 0);
if (user1TaskInfo.heaterOn)     statusByte |= (1 << 1);
if (user1TaskInfo.dryBurnFlag)  statusByte |= (1 << 2);
if (user1TaskInfo.overTempFlag) statusByte |= (1 << 3);

remoteInfo.remoteVar_TX[4].var_float   = user1TaskInfo.currentTemp;
remoteInfo.remoteVar_TX[5].var_uint32  = (uint32_t)user1TaskInfo.targetTemp;
remoteInfo.remoteVar_TX[6].var_uint32  = (uint32_t)user1TaskInfo.massageMode;
remoteInfo.remoteVar_TX[7].var_uint32  = (uint32_t)statusByte;
remoteInfo.remoteVar_TX[8].var_uint32  = user1TaskInfo.timerRemainSec;
remoteInfo.remoteVar_TX[9].var_uint32  = user1TaskInfo.motorPwmDuty;
remoteInfo.remoteVar_TX[10].var_uint32 = (uint32_t)user1TaskInfo.faultCode;
/* TX[11]~[15] 不写，保持原值 */
```

**RX 读取（指令接收）：**
```c
uint32_t appTarget = remoteInfo.remoteVar_RX[0].var_uint32;
uint32_t appTimer  = remoteInfo.remoteVar_RX[1].var_uint32;
uint8_t  appMode   = (uint8_t)(remoteInfo.remoteVar_RX[2].var_uint32 & 0xFF);
uint8_t  appPower  = (uint8_t)(remoteInfo.remoteVar_RX[3].var_uint32 & 0xFF);
```

**目标温度范围校验：**
```c
if (appTarget >= 35 && appTarget <= 48) {
  user1TaskInfo.targetTemp = (float)appTarget;
}
```

---

## 六、driver 层修改需求（如需要）

开发前需先浏览 `userDriver/` 确认现有 driver。以下为可能需要新增的场景：

**场景一：温度传感器 driver 不存在**
- 新增 `driver_temp.c / driver_temp.h`
- 提供 `tempInfo.tempValue`（float 类型当前温度）
- 提供 `DRIVER_TEMP_Init()` 和 `DRIVER_TEMP_Update()`
- 内部通过 ADC 读取 NTC 热敏电阻分压，或通过 UART/单总线读取 DS18B20

**场景二：PWM driver 不存在或 API 不足**
- 新增或扩展 PWM driver，提供 `DRIVER_PWM_SetDuty(uint8_t channel, uint8_t duty_percent)` 接口
- channel 0：加热器 PWM（或继电器控制 GPIO，视硬件而定）
- channel 1：按摩电机 PWM

**场景三：水位检测无专用 driver**
- 直接在 task 层通过 stdlib 的 GPIO 读取接口或 HAL_GPIO_ReadPin 读取水位检测引脚
- 若框架 stdlib 提供了 GPIO 读取封装，优先使用 stdlib 接口

---

## 七、输出物要求

Claude Code 完成开发后，必须输出：

### 1. 代码文件
- `task_user1.c` / `task_user1.h`
- 若修改了 driver 层，输出修改后的 driver 文件，并在修改处用注释标注：`/* [修改] 原因: ... */`

### 2. 功能实现文档（`implementation_report.md`）

**(a) 功能实现状态清单：**

| 序号 | 功能描述 | 状态 | 说明 |
|------|----------|------|------|
| 1 | 系统开关机（APP控制） | ✅/⚠️/❌ | |
| 2 | PID 温控闭环（±0.5℃精度） | ✅/⚠️/❌ | |
| 3 | 多模式按摩（3档+停止） | ✅/⚠️/❌ | |
| 4 | 定时自动停机 | ✅/⚠️/❌ | |
| 5 | 超温保护（≥50℃断热） | ✅/⚠️/❌ | |
| 6 | 防干烧保护（水位检测） | ✅/⚠️/❌ | |
| 7 | APP 状态实时上报 | ✅/⚠️/❌ | |

**(b) driver 层修改记录（如有修改）：**

| 文件 | 修改类型 | 修改内容 | 原因 |
|------|----------|----------|------|
| | | | |

**(c) 已实现功能的测试步骤：**

对每个已实现的功能，给出具体的测试方法和预期结果，让开发者可以逐项验证。

---

## 八、验证清单（开发完成后自查）

Claude Code 提交代码前，必须确认以下各项：

| 检查项 | 确认方法 |
|--------|----------|
| ✅ func_appcom 无改动 | 对比原文件，无任何修改 |
| ✅ 代码中无 APP 端帧号 | 全文搜索 `0x17`、`0x18`、`0x19`、`0x1A`、`0x21`~`0x24`，均不应出现在 task 层代码 |
| ✅ remoteVar 索引无越界 | TX 写入范围 [4]~[10]，RX 读取范围 [0]~[3]，无越界 |
| ✅ TX[0]~[3] 未用于传递 APP 数据 | TX[0]~[3] 未被赋值为需要上报 APP 的状态数据 |
| ✅ 未整体清零 remoteVar_TX | 无 `memset(remoteInfo.remoteVar_TX, 0, ...)` 类语句 |
| ✅ RX 读取无二次字节序翻转 | task 层无 `__bswap`、手动位移翻转等字节序处理代码 |
| ✅ 无 malloc 调用 | 全文搜索 `malloc`、`calloc`，不应出现 |
| ✅ 注释为中文 | 所有新增注释使用中文 |
| ✅ task_system 未修改 | 对比原文件，无任何修改 |
