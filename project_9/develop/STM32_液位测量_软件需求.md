# STM32 软件需求文档 — 超声波液位高度测量系统（含 APP 通信）

---

## 一、项目概述

本项目基于 STM32F103C8T6，使用双超声波测距模块实现非接触式液位高度测量，结果通过 OLED 本地显示，同时经 BLE（ESP32 桥接）上报至手机 APP，并接收 APP 下发的报警阈值设置。

**系统安装方式**：测量装置固定在水箱顶部。

**双模块测距原理**：
- **高精度超声波模块**：实时测量装置距液面的距离（`distToLiquid_mm`）
- **普通超声波模块**：上电标定阶段测量装置距地面的距离（`distToGround_mm`），标定完成后不再使用
- **液位高度计算**：`liquidLevel_mm = distToGround_mm - distToLiquid_mm`

**核心功能**：
1. 上电自动标定地面距离基准值
2. 实时液位高度测量（范围 5~100 cm，精度 ±3 mm）
3. 液位变化量计算（分辨率 1 mm）
4. OLED 本地显示（液位高度、变化量、距液面距离、报警阈值、系统状态）
5. APP 通信：实时上报测量数据，接收并应用 APP 下发的报警阈值
6. 滑动均值滤波，抑制偶然误测

**暂不实现**：蜂鸣器/LED 硬件报警输出（后续补充），但报警阈值的接收、存储、OLED 显示须完整实现。

---

## 二、开发前置步骤

**Claude Code 开发前必须执行以下步骤：**

1. 浏览 `MDK-ARM/userDriver/` 目录，列出所有 `.c` / `.h` 文件
2. 逐个阅读每个 driver 的 `.h` 文件中「向上提供」部分（公有结构体、Init / Update / 功能函数）
3. 汇总所有可用的结构体和 API，重点关注：
   - 超声波测距相关 driver（高精度版本和普通版本，注意区分）
   - OLED 显示 driver 及其字符串/数字显示 API
4. 确认可用 API 后，再开始 task 层设计

---

## 二（附）、禁止修改的文件清单

| 文件 | 原因 |
|------|------|
| `func_appcom.c` / `func_appcom.h` | 帧 CMD 编号（TX: 0x09~0x0C，RX: 0x13~0x16）和帧数量（4TX+4RX）是框架与 ESP32 固件的固定约定，修改会导致 BLE 断连 |
| `task_system.c` / `task_system.h` | 系统任务，框架已实现，不需要修改 |
| `userLib/` 下所有文件 | stdlib 层，框架已实现，不可修改 |
| `Core/` 下所有文件 | HAL 层，CubeMX 生成，不可修改 |

**强制注意事项**：
- 代码中**严禁出现** `0x17`、`0x18`、`0x19`、`0x1A`、`0x21`、`0x22`、`0x23`、`0x24` 等 APP 端帧号，STM32 端只操作 `remoteVar_TX[x]` / `remoteVar_RX[x]` 索引
- **不要整体清零** `remoteVar_TX`，只写本项目使用的索引，未使用索引保持原值

---

## 三、task 层设计

### 1. 文件规划

只使用 `task_user1.c` / `task_user1.h`，包含全部应用逻辑。

### 2. 公有结构体设计

```c
// task_user1.h
typedef struct {
    uint32_t taskCnt;               // 任务计数，用于取余调度

    /* 测距原始数据 */
    float distToGround_mm;          // 普通超声波：模块距地面距离（mm），上电标定后固定
    float distToLiquid_mm;          // 高精度超声波：模块距液面距离（mm），实时更新

    /* 液位计算结果 */
    float liquidLevel_mm;           // 当前液位高度（mm）
    float liquidLevelInit_mm;       // 初始液位高度（mm），标定完成时记录
    float liquidLevelDelta_mm;      // 液位变化量（mm）= liquidLevel - liquidLevelInit

    /* 报警阈值（从 APP 接收，OLED 同步显示） */
    float alarmHigh_mm;             // 液位高报警阈值（mm），默认 0（未设置）
    float alarmLow_mm;              // 液位低报警阈值（mm），默认 0（未设置）

    /* 标定状态 */
    uint8_t calibDone;              // 标定完成标志（0=校准中，1=正常工作）
    uint8_t calibCnt;               // 已采集标定样本数
    float   calibSum;               // 标定累加和

    /* 滑动均值滤波缓冲区（窗口=5） */
    float filterBuf[5];             // 最近5次有效液位高度值
    uint8_t filterIdx;              // 滑动写入索引
    uint8_t filterFull;             // 缓冲区已填满标志

} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;
void user1TaskInit(void);
void user1TaskUpdata(void *argument);
```

### 3. Init 函数设计

```c
void user1TaskInit(void) {
    // 初始化所有需要的 driver（函数名以 userDriver/ 中实际 .h 为准）
    DRIVER_超声波高精度_Init();
    DRIVER_超声波普通_Init();
    DRIVER_OLED_Init();

    // 初始化内部状态
    user1TaskInfo.calibDone          = 0;
    user1TaskInfo.calibCnt           = 0;
    user1TaskInfo.calibSum           = 0.0f;
    user1TaskInfo.distToGround_mm    = 0.0f;
    user1TaskInfo.distToLiquid_mm    = 0.0f;
    user1TaskInfo.liquidLevel_mm     = 0.0f;
    user1TaskInfo.liquidLevelInit_mm = 0.0f;
    user1TaskInfo.liquidLevelDelta_mm= 0.0f;
    user1TaskInfo.alarmHigh_mm       = 0.0f;
    user1TaskInfo.alarmLow_mm        = 0.0f;
    user1TaskInfo.filterIdx          = 0;
    user1TaskInfo.filterFull         = 0;
    for (uint8_t i = 0; i < 5; i++) {
        user1TaskInfo.filterBuf[i] = 0.0f;
    }
}
```

### 4. Updata 循环设计

基础周期 2ms，各子任务通过 `taskCnt` 取余调度：

| 周期 | 取余条件（基础2ms） | 执行内容 |
|------|-------------------|---------|
| 2ms | 每次 | 调用需要高频 Update 的超声波 driver |
| 50ms | `% 25 == 0` | 读取超声波 → 液位计算/标定 → 读取 APP 阈值 |
| 100ms | `% 50 == 0` | 上报数据到 `remoteVar_TX` |
| 200ms | `% 100 == 0` | 刷新 OLED 显示 |

**循环结构**：

```c
void user1TaskUpdata(void *argument) {
    user1TaskInit();
    for (;;) {
        user1TaskInfo.taskCnt++;

        // 2ms：驱动超声波状态机
        DRIVER_超声波高精度_Update();
        DRIVER_超声波普通_Update();

        // 50ms：测距与计算
        if (user1TaskInfo.taskCnt % 25 == 0) {
            if (user1TaskInfo.calibDone == 0) {
                // 功能模块1：上电标定
            } else {
                // 功能模块2：液位实时测量与计算
                // 功能模块3：读取 APP 报警阈值
            }
        }

        // 100ms：上报数据至 APP
        if (user1TaskInfo.taskCnt % 50 == 0) {
            // 功能模块4：APP 数据上报
        }

        // 200ms：刷新 OLED
        if (user1TaskInfo.taskCnt % 100 == 0) {
            // 功能模块5：OLED 显示
        }

        osDelay(2);
    }
}
```

---

## 四、APP 通信变量布局

### TX（STM32 → APP）

| remoteVar_TX 索引 | 有效宽度 | 变量含义 | 数据类型 |
|------------------|---------|---------|---------|
| [0] | 4B | 当前液位高度（mm） | float |
| [1] | 4B | 液位变化量（mm，正=上升，负=下降） | float |
| [2] | 1B | 系统状态（0=校准中，1=正常工作） | uint8_t（var_uint32低8位） |
| [3] | 1B | 保留（填0） | — |
| [4] | 4B | 模块距液面原始距离（mm） | float |
| [5] | 4B | 当前报警上限回显（mm） | float |
| [6] | 1B | 保留（填0） | — |
| [7] | 1B | 保留（填0） | — |
|  |  |  |  |
|  |  |  |  |
|  |  |  |  |
|  |  |  |  |

### RX（APP → STM32）

| remoteVar_RX 索引 | 有效宽度 | 变量含义 | 数据类型 |
|------------------|---------|---------|---------|
| [0] | 4B | APP 设置的液位高报警阈值（mm） | float |
| [1] | 4B | APP 设置的液位低报警阈值（mm） | float |
| [2] | 1B | 保留 | — |
| [3] | 1B | 保留 | — |

> 注：TX[5] 将当前生效的报警上限回显给 APP，用于 APP 侧确认设置已生效。暂只回显高限（上限），低限暂不回显（可后续扩展到 TX[8]）。

---

## 五、功能详细需求

### 功能模块 1：上电地面距离标定

**功能描述**：上电后用普通超声波采集10次数据取均值，确定地面距离基准值，同时记录初始液位高度。

**实现逻辑**（每50ms执行，`calibDone == 0` 期间）：

```
1. 读取普通超声波当前距地面距离值 → rawGround
2. 若读数有效（rawGround > 0），累加到 calibSum，calibCnt++
3. 当 calibCnt >= CALIB_SAMPLE_COUNT（= 10）时：
   a. distToGround_mm = calibSum / CALIB_SAMPLE_COUNT
   b. 读取一次高精度超声波 → distToLiquid_mm
   c. liquidLevelInit_mm = distToGround_mm - distToLiquid_mm
   d. liquidLevel_mm = liquidLevelInit_mm
   e. liquidLevelDelta_mm = 0.0f
   f. calibDone = 1
```

**关键参数**：
- `CALIB_SAMPLE_COUNT = 10`（宏定义在 `.c` 文件中）
- 采样间隔 50ms，总标定时间约 500ms

---

### 功能模块 2：液位高度实时测量与计算

**功能描述**：标定完成后每50ms读取高精度超声波，滤波后计算液位高度和变化量。

**实现逻辑**：

```
1. 读取高精度超声波距液面距离 → rawDist（单位 mm）
2. 计算原始液位：rawLevel = distToGround_mm - rawDist
3. 有效性校验：若 rawLevel < 50.0f 或 rawLevel > 1000.0f，丢弃本次数据
4. 滑动均值滤波（窗口5）：
   - 写入 filterBuf[filterIdx]，filterIdx = (filterIdx+1) % 5
   - filterIdx 回绕为0时 filterFull = 1
   - 均值 = sum(filterBuf[0..4]) / (filterFull ? 5 : filterIdx)
5. liquidLevel_mm = 滤波后均值
6. distToLiquid_mm = rawDist（更新原始距离，供 OLED 和 APP 显示）
7. liquidLevelDelta_mm = liquidLevel_mm - liquidLevelInit_mm
```

**关键参数**：
- 有效范围：50 mm ~ 1000 mm
- 滤波窗口：5

---

### 功能模块 3：读取 APP 报警阈值

**功能描述**：实时读取 APP 下发的报警阈值，若有效则更新本地存储值，OLED 刷新时同步显示。

**实现逻辑**（每50ms执行，仅在 `calibDone == 1` 后执行）：

```c
/* 读取 APP 下发高限，仅当值 > 0 时认为用户有设置意图 */
float rxHigh = remoteInfo.remoteVar_RX[0].var_float;
float rxLow  = remoteInfo.remoteVar_RX[1].var_float;

if (rxHigh > 0.0f) {
    user1TaskInfo.alarmHigh_mm = rxHigh;
}
if (rxLow > 0.0f) {
    user1TaskInfo.alarmLow_mm = rxLow;
}
```

> APP 端初始不发送阈值（默认字段为0），STM32 端以 `> 0` 为判断依据，避免误覆盖。

---

### 功能模块 4：APP 数据上报

**功能描述**：每100ms将测量结果写入 `remoteVar_TX`，由 `task_system` 自动通过 appcom 发出。

**实现逻辑**：

```c
/* 仅写本项目使用的索引，未使用的索引不操作 */
remoteInfo.remoteVar_TX[0].var_float   = user1TaskInfo.liquidLevel_mm;
remoteInfo.remoteVar_TX[1].var_float   = user1TaskInfo.liquidLevelDelta_mm;
remoteInfo.remoteVar_TX[2].var_uint32  = (uint32_t)user1TaskInfo.calibDone;
// [3] 不操作（保留）
remoteInfo.remoteVar_TX[4].var_float   = user1TaskInfo.distToLiquid_mm;
remoteInfo.remoteVar_TX[5].var_float   = user1TaskInfo.alarmHigh_mm;
// [6][7] 不操作（保留）
```

---

### 功能模块 5：OLED 显示

**功能描述**：每200ms刷新 OLED，分两个状态显示。

**状态一：校准中（`calibDone == 0`）**

```
行0: 液位测量系统
行1: 正在校准...
行2: 请稍候
行3:（空）
```

**状态二：正常工作（`calibDone == 1`）**

```
行0: 液位: XXXX mm
行1: 变化: +XXX mm
行2: 距液: XXXX mm
行3: 阈值: XXXX mm  （显示高限，未设置则显示 ----）
```

**实现要点**：
- 使用栈上 `char buf[32]` + `sprintf` 格式化字符串，禁止 malloc
- 数值显示精度：液位高度取整到 mm，变化量带符号显示（`%+d`）
- 阈值未设置时（`alarmHigh_mm <= 0`）显示 `"阈值:  ----"`
- OLED driver API 以 `userDriver/` 中实际提供的函数为准

---

## 六、driver 层修改需求

**暂定无需修改**，以实际 driver 提供的 API 为准。若存在以下情况允许扩展：

| 不足场景 | 处理方式 |
|---------|---------|
| OLED driver 无字符串显示函数 | 在 driver 层扩展 `DRIVER_OLED_ShowString(x, y, str)` 和 `DRIVER_OLED_ShowNum(x, y, num)` |
| 超声波 driver 不支持两路独立实例 | 复制 driver 文件，重命名为高精度版和普通版，分别配置引脚 |

修改 driver 时必须遵守命名规范（`DRIVER_XXX_FuncName`），公有声明写 `.h`，实现写 `.c`，修改处加注释标注。

---

## 七、输出物要求

### 1. 代码文件

- `userApp/task_user1.c`
- `userApp/task_user1.h`
- 如修改了 driver，输出修改后的文件并在修改处注释 `/* [新增/修改] 原因：xxx */`

### 2. 功能实现文档（`implementation_report.md`）

#### (a) 功能实现状态清单

| 序号 | 功能描述 | 状态 | 说明 |
|------|---------|------|------|
| 1 | 上电地面距离标定 | ✅/⚠️/❌ | |
| 2 | 液位高度实时测量与计算 | ✅/⚠️/❌ | |
| 3 | 液位变化量计算 | ✅/⚠️/❌ | |
| 4 | APP 报警阈值接收与存储 | ✅/⚠️/❌ | |
| 5 | APP 数据上报 | ✅/⚠️/❌ | |
| 6 | OLED 显示 | ✅/⚠️/❌ | |
| 7 | 滑动均值滤波 | ✅/⚠️/❌ | |

#### (b) driver 层修改记录

| 文件 | 修改类型 | 修改内容 | 原因 |
|------|---------|---------|------|
| — | — | — | — |

#### (c) 测试步骤

| 功能 | 测试方法 | 预期结果 |
|------|---------|---------|
| 上电标定 | 上电后观察 OLED 第1行，等待校准完成 | 约500ms后切换为正常显示 |
| 液位测量 | 用卷尺测量实际液位，与 OLED 对比 | 误差 ≤ ±3 mm |
| 液位变化量 | 记录初始值后向水箱加水约50mm | 变化量显示约 +50，误差 ≤ ±3 mm |
| 滤波效果 | 手在液面上方快速晃动 | OLED 显示无剧烈跳变（ ≤ ±5 mm） |
| APP 阈值接收 | APP 输入高限 800mm，点设置 | OLED 第4行更新为 `阈值: 800 mm` |
| APP 数据显示 | 对比 APP 与 OLED 显示值 | 两端数值一致，误差 ≤ 1 mm |

---

## 八、验证清单（提交前自查）

- [ ] `func_appcom.c/h` 未做任何修改
- [ ] `task_system.c/h` 未做任何修改
- [ ] `userLib/` 下所有文件未修改
- [ ] `Core/` 下所有文件未修改
- [ ] 代码中不出现 `0x17~0x1A`、`0x21~0x24` 等 APP 端帧号
- [ ] `remoteVar_TX` 未被整体清零，只写了使用的索引 [0][1][2][4][5]
- [ ] `remoteVar_TX/RX` 索引访问范围在 [0]~[15] 内
- [ ] 全部注释使用中文
- [ ] 无 `malloc` / `calloc` / `realloc` 调用
- [ ] 私有定义在 `.c`，公有定义在 `.h`
- [ ] task 层未直接调用 `HAL_xxx` 函数

---

*文档版本：v1.0 | 基于需求生成模板 v5*
