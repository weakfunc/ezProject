# STM32 软件需求文档
## 盲人电子超声波测距系统

> 本文档供 STM32 项目目录下的 Claude Code 执行开发使用。

---

## 一、项目概述

本项目为面向视障人士的电子超声波测距辅助系统，基于 STM32F103C8T6 主控，集成以下核心功能：

1. **超声波测距**：实时测量障碍物距离（2~600 cm，精度 ≤0.1 cm），在 OLED 液晶屏上显示
2. **语音播报**：通过语音模块 API 播报当前距离及行走提示
3. **GPS 定位**：通过 GPS 模块获取坐标，并通过 SIM800（GSM/GPRS）模块向家人手机发送位置短信
4. **跌倒检测**：利用加速度传感器数据融合判断是否摔倒，摔倒后自动发送短信报警至预设手机号
5. **OLED 显示**：综合显示距离、GPS 状态、跌倒状态、系统状态等信息，尽可能覆盖每个模块
6. **整点报时**：每小时整点通过语音模块播报当前时间（需时钟模块支持）

> **不实现的功能**：湿度检测（补充说明中明确排除）、手机号 APP 设置（无 APP 需求，手机号在代码中静态配置）

---

## 二、开发前置步骤

**Claude Code 开发前必须执行以下步骤，不可跳过：**

1. 浏览 `MDK-ARM/userDriver/` 目录，列出所有保留的 `.c` / `.h` 文件
2. 逐个阅读每个 driver 的 `.h` 文件中「向上提供」部分，汇总：
   - 公有信息结构体（`xxxInfo` / `xxxInfo[CH_COUNT]`）
   - 初始化函数 `DRIVER_XXX_Init()`
   - 更新函数 `DRIVER_XXX_Update()`（如有）
   - 其他功能函数
3. 确认以下模块的 driver 均存在（根据任务需求）：
   - 超声波测距模块
   - OLED 显示模块
   - 语音播报模块
   - GPS 定位模块
   - SIM800 短信模块
   - 时钟（RTC）模块
   - 加速度传感器模块（用于跌倒检测）
4. 以汇总的 API 为基础进行 task 层设计，**不得假设 API 名称，必须以实际 .h 文件为准**

---

## 二（附）、禁止修改的文件清单

以下文件**严禁修改**：

| 文件 | 禁止原因 |
|------|----------|
| `func_appcom.c` / `func_appcom.h` | 帧 CMD 编号（TX: 0x09~0x0C，RX: 0x13~0x16）和帧数量（4TX+4RX）是框架与 ESP32 固件的固定约定，修改会导致 BLE 断连 |
| `task_system.c` / `task_system.h` | 系统任务，框架已实现，负责 stdlib 状态机维护和 appcom 通信更新 |
| `userLib/` 下所有文件 | stdlib 层，框架已实现，不可修改 |
| `Core/` 下所有文件 | HAL 层，CubeMX 生成，不可修改 |

**额外注意事项：**
- 本项目无 APP 需求，`remoteVar_TX` / `remoteVar_RX` 无需使用，但不得整体清零
- 代码中**不得出现** `0x17~0x1A`、`0x21~0x24` 等 APP 端帧号（STM32 端不需要知道这些）
- 禁止 `malloc`，全部静态分配
- 注释语言：**中文**
- 命名风格：task 层小驼峰（`user1TaskInit`）；结构体 `xxxTaskInfo_t`；宏 `MODULE_CONSTANT`

---

## 三、task 层设计

### 1. 文件规划

仅使用默认的 `task_user1.c` / `task_user1.h`，在其中实现全部应用逻辑。

### 2. 公有结构体设计

```c
// task_user1.h
typedef struct {
    uint32_t taskCnt;           // 任务计数，用于多周期调度

    // 超声波测距
    float    distance_cm;       // 当前测距值（cm）

    // 跌倒检测
    uint8_t  isFallen;          // 0=正常，1=检测到摔倒
    uint8_t  fallAlertSent;     // 摔倒短信是否已发送（防重复）

    // GPS
    uint8_t  gpsFixed;          // 0=未定位，1=已定位
    float    latitude;          // 纬度
    float    longitude;         // 经度

    // 整点报时
    uint8_t  lastHour;          // 上次报时的小时数，防重复

    // 语音播报节流
    uint8_t  voiceBusy;         // 1=语音正在播报，防重叠（如 driver 提供状态则用 driver 状态）

    // OLED 刷新控制
    uint8_t  oledNeedUpdate;    // 标志位，指示需要刷新 OLED

} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;
void user1TaskInit(void);
void user1TaskUpdata(void *argument);
```

### 3. Init 函数设计

```c
void user1TaskInit(void) {
    DRIVER_Ultrasonic_Init();   // 超声波测距
    DRIVER_OLED_Init();         // OLED 显示
    DRIVER_Voice_Init();        // 语音播报
    DRIVER_GPS_Init();          // GPS 定位
    DRIVER_SIM800_Init();       // SIM800 短信/通话
    DRIVER_RTC_Init();          // 时钟模块
    DRIVER_Accel_Init();        // 加速度传感器

    // 初始化状态
    user1TaskInfo.taskCnt       = 0;
    user1TaskInfo.isFallen      = 0;
    user1TaskInfo.fallAlertSent = 0;
    user1TaskInfo.gpsFixed      = 0;
    user1TaskInfo.lastHour      = 0xFF; // 无效初值，确保首次整点触发
    user1TaskInfo.voiceBusy     = 0;
    user1TaskInfo.oledNeedUpdate = 0;
}
```

> **注意**：上述 Init 函数名称均为占位符，必须以 `userDriver/` 中实际 `.h` 文件的 API 名称为准。

### 4. Updata 循环设计

基础周期：**2ms**（`osDelay(2)`）

| taskCnt 取余 | 实际周期 | 执行内容 |
|---|---|---|
| `% 5 == 0` | 10ms | 超声波测距 Update；加速度 Update；跌倒检测逻辑 |
| `% 25 == 0` | 50ms | 语音播报距离节流判断；OLED 刷新 |
| `% 250 == 0` | 500ms | GPS Update；整点报时检查 |
| `% 500 == 0` | 1s | SIM800 状态检查；跌倒短信发送（若 isFallen 且未发送） |

---

## 四、APP 通信变量布局

本项目**无 APP 需求**，不使用 `remoteVar_TX` / `remoteVar_RX`。

无需向 `remoteVar_TX` 写入任何数据，无需读取 `remoteVar_RX`。

---

## 五、功能详细需求

### 5.1 超声波测距

**功能描述：**  
实时测量前方障碍物距离，范围 2~600 cm，精度 ≤0.1 cm，结果显示在 OLED 上并周期语音播报。

**实现逻辑：**
- 在 10ms 周期调用 `DRIVER_Ultrasonic_Update()`（或等效触发函数）更新测距
- 读取 `ultrasonicInfo.distance_cm`（以实际结构体字段为准）存入 `user1TaskInfo.distance_cm`
- 距离分级语音播报（50ms 周期判断，但播报需节流，前次播报完成后再触发）：
  - distance < 50 cm：紧急警告，播报"前方障碍物，请注意"
  - 50 cm ≤ distance < 100 cm：播报"前方约XX厘米有障碍"
  - distance ≥ 100 cm：可选择降低播报频率（每500ms一次）或不播报
- 语音播报内容已由框架提供，调用对应 API 传入内容字符串即可

**涉及 driver API：**
- `DRIVER_Ultrasonic_Init()`
- `DRIVER_Ultrasonic_Update()`
- `ultrasonicInfo.distance_cm`（以实际字段名为准）
- 语音播报 API（见 5.6）

**关键参数：**
- 危险距离阈值：50 cm
- 警示距离阈值：100 cm

---

### 5.2 OLED 显示

**功能描述：**  
OLED 屏综合显示系统各模块状态，信息尽可能覆盖每个模块。

**显示布局设计（按行分配，具体行数以实际 OLED 分辨率调整）：**

| 行 | 显示内容 | 示例 |
|---|---|---|
| 第1行 | 距离信息 | `Dist: 125.3 cm` |
| 第2行 | GPS 状态 + 坐标 | `GPS:OK 31.23 121.47` 或 `GPS:搜索中` |
| 第3行 | 跌倒状态 | `Fall:正常` 或 `Fall:!摔倒!` |
| 第4行 | 时间（RTC） | `2025-01-01 10:30` |
| 第5行（如有） | SIM800状态 | `SIM:OK` 或 `SIM:无信号` |

**实现逻辑：**
- 50ms 周期刷新 OLED（避免过高刷新率导致闪烁）
- 每次刷新前先清屏（或逐行覆写），再写入最新数据
- 字段格式化为定长字符串，防止残留旧数据

**涉及 driver API：**
- `DRIVER_OLED_Init()`
- OLED 清屏、写字符串等函数（以实际 API 为准）

---

### 5.3 语音播报（距离提示 + 行走引导）

**功能描述：**  
通过语音模块播报障碍物距离及行走建议，提示盲人如何行走。语音内容已由系统提供，调用 API 传入字符串即可。

**实现逻辑：**
- 语音播报需节流，避免同时触发多条语音：
  - 检查 `voiceBusy` 状态（或 driver 提供的忙碌标志）
  - 只有上一条播完后才能触发下一条
- 播报优先级（高→低）：跌倒报警 > 危险距离 > 普通距离提示 > 整点报时
- 语音内容示例（以实际提供的内容为准）：
  - "前方有障碍物，距离约XX厘米，请注意"
  - "请向左转"（如有方向提示需求）
  - "现在是X点整"（整点报时）
  - "检测到摔倒，正在报警"

**涉及 driver API：**
- `DRIVER_Voice_Init()`
- `DRIVER_Voice_Play(const char *text)` 或等效 API（以实际为准）
- 语音忙碌状态查询 API（以实际为准）

---

### 5.4 GPS 定位

**功能描述：**  
获取当前 GPS 坐标（经纬度），用于 OLED 显示及发短信给家人。

**实现逻辑：**
- 500ms 周期调用 GPS Update，解析 NMEA 数据
- 读取 `gpsInfo.latitude` / `gpsInfo.longitude` / `gpsInfo.fixed`（以实际字段名为准）
- 定位状态存入 `user1TaskInfo.gpsFixed`
- 定位成功后，坐标用于短信内容拼接

**涉及 driver API：**
- `DRIVER_GPS_Init()`
- `DRIVER_GPS_Update()`
- `gpsInfo.latitude`、`gpsInfo.longitude`、`gpsInfo.fixed`（以实际为准）

---

### 5.5 短信报警（SIM800）

**功能描述：**  
检测到摔倒后，自动通过 SIM800 模块向预设家人手机号发送位置短信，包含 GPS 坐标。

**实现逻辑：**
- 预设手机号在代码顶部以宏定义静态配置，例如：
  ```c
  #define ALERT_PHONE_NUMBER  "138XXXXXXXX"
  ```
- 短信内容格式：
  ```
  "紧急警报！用户摔倒！当前位置：纬度XX.XXXX，经度XXX.XXXX"
  ```
  若 GPS 未定位，则：
  ```
  "紧急警报！用户摔倒！GPS未定位，无法获取坐标。"
  ```
- 防重复发送机制：
  - `fallAlertSent = 0` 时才发送
  - 发送成功后置 `fallAlertSent = 1`
  - 当 `isFallen` 重新变为 0（恢复正常）后，重置 `fallAlertSent = 0`，允许下次再触发
- 发送时机：1s 周期检查，发现 `isFallen == 1 && fallAlertSent == 0` 时发送

**涉及 driver API：**
- `DRIVER_SIM800_Init()`
- `DRIVER_SIM800_SendSMS(const char *phoneNum, const char *message)` 或等效 API（以实际为准）

---

### 5.6 跌倒检测（加速度融合）

**功能描述：**  
利用加速度传感器数据融合算法判断用户是否摔倒，检测到摔倒后触发报警流程。

**实现逻辑（加速度融合判断）：**

> 以下为通用跌倒判断逻辑，若 driver 已提供跌倒检测 API，则优先使用 driver 提供的接口。

**两阶段判断法：**

**阶段一：冲击检测（合加速度突变）**
- 计算合加速度：`G = sqrt(ax² + ay² + az²)`
- 若 `G > FALL_THRESHOLD_HIGH`（建议阈值：3.0g，即约 29.4 m/s²），认为发生冲击

**阶段二：静止确认（姿态保持低动态）**
- 冲击发生后，连续观察 500ms（约25个10ms周期）
- 若期间 `G < FALL_THRESHOLD_STATIC`（建议阈值：1.5g）且持续低动态，则确认摔倒
- 若期间动态恢复正常，则取消报警（误判）

**状态机：**
```
NORMAL → (G > 3.0g) → IMPACT_DETECTED → (静止500ms) → FALLEN
IMPACT_DETECTED → (动态恢复) → NORMAL
FALLEN → (手动复位 或 重新运动检测恢复) → NORMAL
```

**关键宏定义：**
```c
#define FALL_THRESHOLD_HIGH    3.0f   // 冲击阈值（g）
#define FALL_THRESHOLD_STATIC  1.5f   // 静止判断阈值（g）
#define FALL_CONFIRM_COUNT     50     // 50 × 10ms = 500ms 静止确认时长
```

**执行周期：** 10ms

**涉及 driver API：**
- `DRIVER_Accel_Init()`
- `DRIVER_Accel_Update()`
- `accelInfo.ax`、`accelInfo.ay`、`accelInfo.az`（单位 g，以实际字段为准）
- 若 driver 已提供 `accelInfo.isFallen` 或等效字段，直接读取，不必重复实现上述算法

---

### 5.7 整点报时

**功能描述：**  
每小时整点通过语音模块播报当前时间，例如"现在是十点整"。

**实现逻辑：**
- 500ms 周期读取 RTC 时间，获取当前小时 `currentHour` 和分钟 `currentMinute`
- 当 `currentMinute == 0 && currentHour != lastHour` 时触发报时：
  - 播报当前时间（调用语音 API）
  - 更新 `lastHour = currentHour`，防止同一小时重复触发
- 播报优先级低于跌倒报警和危险距离，若语音忙碌则跳过（下一分钟内不再补报）

**涉及 driver API：**
- `DRIVER_RTC_Init()`
- `DRIVER_RTC_GetTime()` 或 `rtcInfo.hour` / `rtcInfo.minute`（以实际为准）
- 语音播报 API

---

## 六、driver 层修改需求（如需要）

当前判断：所有功能（测距、OLED、语音、GPS、SIM800、RTC、加速度）均已由 driver 文件夹提供实现。

**原则上不需要修改 driver 层**，task 层直接调用现有 API 即可。

**例外情况（开发时按需判断）：**

| 场景 | 可能需要的修改 | 说明 |
|------|--------------|------|
| 语音 driver 仅支持播放文件编号，不支持字符串 | 扩展 `DRIVER_Voice_PlayText()` | 若语音内容以编号方式预存在模块中，则改为调用对应编号 |
| 加速度 driver 未提供合加速度计算 | 在 task 层自行计算 `sqrt(ax²+ay²+az²)` | 不修改 driver，直接在 task 层使用 math.h |
| GPS driver 未提供定位状态标志 | 扩展 `gpsInfo.fixed` 字段 | 若需修改 driver，必须在实现报告中标注 |

若确实需要修改 driver，必须：
1. 遵守 driver 层接口规范（公有信息结构体 + Init + Update + 功能函数）
2. 命名规范：`DRIVER_XXX_FunctionName()`
3. 在 `implementation_report.md` 中明确标注修改了哪个文件的哪些内容

---

## 七、验证清单

Claude Code 完成开发后，**必须逐项检查以下内容，并在实现报告中确认**：

| 序号 | 检查项 | 确认 |
|------|--------|------|
| 1 | `func_appcom.c` / `func_appcom.h` 未做任何修改 | □ |
| 2 | 代码中无 `0x17`~`0x1A`、`0x21`~`0x24` 等 APP 端帧号 | □ |
| 3 | `remoteVar_TX` / `remoteVar_RX` 未被整体清零 | □ |
| 4 | `TX[0]~[3]` 未用于向 APP 发送数据（本项目无 APP，此项自动满足） | □ |
| 5 | 未使用 `malloc`，全部静态分配 | □ |
| 6 | 注释使用中文 | □ |
| 7 | 私有变量/函数定义在 `.c` 文件中，公有定义在 `.h` 文件中 | □ |
| 8 | `task_system.c` 未被修改 | □ |
| 9 | `userLib/` 和 `Core/` 下文件未被修改 | □ |
| 10 | 跌倒检测逻辑已实现（加速度融合或 driver 直接支持） | □ |
| 11 | 短信报警有防重复机制（`fallAlertSent` 标志） | □ |
| 12 | 预设手机号以宏定义方式配置，未硬编码在逻辑中 | □ |

---

## 八、输出物要求

Claude Code 完成开发后，**必须输出**：

### 1. 代码文件

- `task_user1.c`
- `task_user1.h`
- 若修改了 driver 层：输出修改后的 driver 文件，并用注释 `/* [修改] 原因：xxx */` 标注修改点

### 2. 功能实现文档（`implementation_report.md`）

包含以下内容：

#### （a）功能实现状态清单

| 序号 | 功能描述 | 状态 | 说明 |
|------|----------|------|------|
| 1 | 超声波测距（2~600cm，精度≤0.1cm） | ✅/⚠️/❌ | |
| 2 | OLED 综合显示（覆盖所有模块） | ✅/⚠️/❌ | |
| 3 | 语音播报距离及行走提示 | ✅/⚠️/❌ | |
| 4 | GPS 定位获取坐标 | ✅/⚠️/❌ | |
| 5 | SIM800 短信发送位置给家人 | ✅/⚠️/❌ | |
| 6 | 跌倒检测（加速度融合）+ 短信报警 | ✅/⚠️/❌ | |
| 7 | 整点报时 | ✅/⚠️/❌ | |

状态说明：✅ 已实现 / ⚠️ 部分实现（说明缺什么）/ ❌ 未实现（说明原因）

#### （b）driver 层修改记录（如有）

| 文件 | 修改类型 | 修改内容 | 原因 |
|------|----------|----------|------|

#### （c）已实现功能的测试步骤

对每个已实现功能，给出具体测试方法和预期结果，例如：

- **测距功能**：将手掌放于超声波传感器前约 30cm 处，OLED 应显示 `Dist: ~30.0 cm`，语音应在距离 < 50cm 时播报警告
- **跌倒检测**：快速倾斜/摔落设备，保持静止 500ms 以上，OLED 应显示 `Fall:!摔倒!`，语音播报报警，SIM800 发送短信
- **整点报时**：将 RTC 时间调至 XX:59:50，等待过整点，语音应播报对应时间
- **GPS 短信**：在开阔户外等待 GPS 定位，触发跌倒检测后，手机应收到含坐标的短信
