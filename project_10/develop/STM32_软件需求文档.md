# STM32 软件需求文档 — 宠物智能饲喂系统

> 本文档供 STM32 项目目录下的 Claude Code 阅读并执行开发。文档自包含，不需要参考其他资料。

---

## 一、项目概述

本项目为基于 STM32F103C8T6 的**宠物智能饲喂系统**。系统通过称重传感器实时检测食槽余量，通过红外传感器检测宠物接近状态，通过 RFID 模块识别宠物身份，通过 360 度舵机控制食槽开关完成投喂，通过蜂鸣器实现本地报警，通过 BLE（经 ESP32 透传）与手机 APP 双向通信。

**核心功能**：
1. 实时检测食槽余量与投喂状态
2. 自动按计划定时投喂（最多 5 个计划，时段无冲突）
3. RFID 识别宠物个体并执行对应投喂方案
4. 接收 APP 远程控制（增/删/改投喂计划、手动控制舵机、设置报警阈值、启用/禁用计划执行）
5. Flash 存储投喂计划与历史记录，断电恢复
6. 三类异常报警（食槽余量不足、电机卡堵、外部断电）
7. 通过 BLE 持续上报状态与计划列表给 APP

---

## 二、开发前置步骤

**Claude Code 在开始编码前必须先完成以下步骤**：

1. 浏览 `MDK-ARM/userDriver/` 目录，列出所有 `.c/.h` 文件
2. 逐个阅读每个 driver 模块的 `.h` 文件中「向上提供」部分（公有结构体、API 函数）
3. 汇总以下 driver 的可用接口（项目中保留的 driver 即为本项目可用模块）：
   - 称重传感器 driver（HX711 或类似）
   - 红外传感器 driver
   - 舵机 driver（360 度连续旋转舵机）
   - 蜂鸣器 driver
   - RFID driver（RC522 或类似）
   - Flash driver（W25Q 或片内 Flash）
   - 时间/RTC driver（如有）
   - LED driver（如有）
4. 以汇总的 driver API 为基础，再开始 task 层设计。**禁止假设不存在的 API**。
5. 如发现某个功能仅靠现有 driver API 无法实现，按本文档「六、driver 层修改需求」部分处理。

---

## 二（附）、禁止修改的文件清单

| 文件 | 禁止修改原因 |
|---|---|
| `userFunc/func_appcom.c` / `func_appcom.h` | 帧 CMD 编号（TX: 0x09~0x0C, RX: 0x13~0x16）和帧数量（4TX+4RX）是框架与 ESP32 固件的固定协议。修改会导致 BLE 断连。 |
| `userApp/task_system.c` / `task_system.h` | 系统任务，框架已实现，负责 stdlib 状态机维护和 appcom 通信更新。 |
| `userLib/` 下所有文件 | stdlib 层，框架已实现，不可修改。 |
| `Core/` 下所有文件 | HAL 层，CubeMX 生成，不可修改。 |

**关键注意事项**：

- ⚠️ **TX[0]~[3]（帧 0x09）不会到达 APP**。发送给 APP 的变量必须使用 `remoteVar_TX[4]~[15]`。
- ⚠️ **不要整体清零 `remoteVar_TX`**：只写需要的索引，未使用的保持原值。整体清零可能导致未使用帧发送异常数据。
- ⚠️ **不要在代码中出现 0x17~0x1A、0x21~0x24 等 APP 端帧号**。STM32 端只通过 `remoteVar_TX[i]` / `remoteVar_RX[i]` 读写数据，不需要知道底层帧号。
- ⚠️ **不要在 task 层做任何字节序处理**。读 RX 直接用 `var_float / var_uint32`（func_appcom 已完成翻转），写 TX 直接赋值即可。
- ⚠️ **禁止使用 `malloc`**，全部静态分配。
- ⚠️ **禁止 task 层直接调用 HAL**，必须通过 driver 层。

---

## 三、task 层设计

### 1. 文件规划

只新增 / 修改以下文件：
- `userApp/task_user1.c` 与 `task_user1.h` — 应用主任务（基础周期 2ms）

不新增其他 task 文件，所有应用逻辑集中在 `task_user1` 中。

### 2. 公有结构体设计（`user1TaskInfo_t`）

`task_user1.h` 中定义：

```c
/* 投喂方案最大数量 */
#define USER1_PLAN_MAX_COUNT      5

/* 命令码（来自 APP 的 RX[2]） */
#define USER1_CMD_NONE            0
#define USER1_CMD_PLAN_SET        1   /* 新增或修改方案（按 planId 决定，存在则改，不存在则增） */
#define USER1_CMD_PLAN_DEL        2   /* 删除方案 */
#define USER1_CMD_SERVO_OPEN      3   /* 手动开启舵机（仅在计划禁用时有效） */
#define USER1_CMD_SERVO_CLOSE     4   /* 手动关闭舵机（仅在计划禁用时有效） */
#define USER1_CMD_PLAN_ENABLE     5   /* 启用计划执行 */
#define USER1_CMD_PLAN_DISABLE    6   /* 禁用计划执行 */
#define USER1_CMD_PLAN_SELECT     7   /* APP 主动选择当前执行方案（覆盖 RFID 识别） */
#define USER1_CMD_THRESHOLD_SET   8   /* 设置报警阈值 */

/* 投喂状态 */
#define USER1_FEED_STATE_IDLE     0   /* 空闲，舵机关闭 */
#define USER1_FEED_STATE_FEEDING  1   /* 投喂中，舵机打开 */

/* 报警标志位 */
#define USER1_ALARM_LOW_FOOD      (1U << 0)   /* 食槽余量不足 */
#define USER1_ALARM_MOTOR_JAM     (1U << 1)   /* 电机卡堵 */
#define USER1_ALARM_POWER_OFF     (1U << 2)   /* 外部断电（BLE 未连接判定） */

/* 单条投喂方案 */
typedef struct {
    uint8_t  planId;          /* 方案ID 1~5，0 表示该槽位未使用 */
    uint8_t  reserved[3];     /* 对齐填充 */
    float    frequencySec;    /* 投喂频率（秒），下次投喂距上次的间隔 */
    float    weightG;         /* 单次投喂目标重量（克） */
    uint32_t lastFeedTickMs;  /* 上次投喂的 tick（毫秒，用于计算下次投喂时间） */
} user1Plan_t;

/* 历史投喂记录单条 */
typedef struct {
    uint8_t  planId;          /* 触发该次投喂的方案ID */
    uint8_t  reserved[3];
    float    actualWeightG;   /* 实际投喂重量（克） */
    uint32_t timestampSec;    /* 投喂时间戳（系统秒计数） */
} user1FeedRecord_t;

#define USER1_HISTORY_MAX_COUNT   30  /* 30 天历史记录的简化实现：保存最近 30 条 */

typedef struct {
    /* 任务节拍 */
    uint32_t taskCnt;                 /* 任务计数（基础周期 2ms） */

    /* 传感器数据 */
    float    currentWeightG;          /* 当前食槽重量（g），称重传感器实时值 */
    uint8_t  petNearby;               /* 红外检测到宠物接近：0=否，1=是 */
    uint8_t  rfidPetId;               /* RFID 识别到的宠物ID，0=未识别 */

    /* 投喂控制 */
    uint8_t  feedingState;            /* 当前投喂状态（USER1_FEED_STATE_xxx） */
    uint8_t  planEnabled;             /* 计划执行开关，0=禁用（手动模式），1=启用（计划模式） */
    uint8_t  currentPlanId;           /* 当前执行方案ID，0=无 */
    float    targetWeightG;           /* 当前投喂目标重量（来自当前方案） */
    uint32_t feedStartTickMs;         /* 本次投喂开始 tick（用于卡堵检测） */
    float    feedStartWeightG;        /* 本次投喂开始时的食槽重量 */

    /* 投喂方案列表 */
    user1Plan_t plans[USER1_PLAN_MAX_COUNT];

    /* 历史记录列表（环形缓冲） */
    user1FeedRecord_t history[USER1_HISTORY_MAX_COUNT];
    uint8_t  historyHead;             /* 环形缓冲写入位置 */
    uint8_t  historyCount;            /* 已存数量（≤USER1_HISTORY_MAX_COUNT） */

    /* 报警 */
    float    alarmThresholdG;         /* 食槽余量报警阈值（g），APP 可设置，默认 20g */
    uint8_t  alarmFlags;              /* 报警标志位组合（USER1_ALARM_xxx） */
    uint8_t  beeperOn;                /* 蜂鸣器当前是否鸣响 */

    /* 计划同步广播（向 APP 循环广播本地计划列表） */
    uint8_t  planBroadcastIndex;      /* 当前广播的方案索引 0~USER1_PLAN_MAX_COUNT-1 */

    /* 命令去重 */
    uint8_t  lastCmdSeq;              /* 上次执行的 RX[6] 序列号 */

    /* 系统计时 */
    uint32_t systemSec;               /* 系统秒计数（用于历史记录时间戳与每日计数复位） */
    uint32_t feedCountToday;          /* 当日投喂次数 */

    /* BLE 连接状态（来自 ESP32，用于断电报警判定） */
    uint8_t  bleConnected;            /* 0=未连接（视为断电），1=已连接 */
} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;

void user1TaskInit(void);
void user1TaskUpdata(void *argument);
```

### 3. Init 函数设计

`user1TaskInit()` 在任务循环开始前执行一次：

1. 调用所有需要的 `DRIVER_XXX_Init()`（按 driver 实际命名）：
   - 称重传感器 Init
   - 红外传感器 Init
   - 舵机 Init（初始化为关闭状态）
   - 蜂鸣器 Init
   - RFID Init
   - Flash Init
2. 清零 `user1TaskInfo` 中的运行时字段
3. **从 Flash 读取已存储的投喂计划列表**到 `user1TaskInfo.plans[]`
4. **从 Flash 读取已存储的历史记录**到 `user1TaskInfo.history[]`、`historyHead`、`historyCount`
5. **从 Flash 读取报警阈值**到 `user1TaskInfo.alarmThresholdG`（无效或首次使用时默认 20.0f）
6. **从 Flash 读取 `planEnabled` 标志**（默认为 1）
7. 默认 `currentPlanId = 1`（系统上电默认执行方案 1，需求明确指出）
8. 设舵机为关闭状态，蜂鸣器关闭

### 4. Updata 循环设计

`user1TaskUpdata()` 基础周期 2ms。任务结构如下：

```
void user1TaskUpdata(void *argument) {
    user1TaskInit();
    for(;;) {
        user1TaskInfo.taskCnt++;

        // ========== 周期性 driver 更新（如有） ==========
        // 例：DRIVER_WEIGHT_Update(); 调用频率按 driver 自身要求

        // ========== 10ms 周期 ==========
        if (user1TaskInfo.taskCnt % 5 == 0) {
            // 1. 读取称重传感器，更新 currentWeightG
            // 2. 投喂状态机推进（关键：达到目标重量则关闭舵机）
            // 3. 卡堵检测
        }

        // ========== 50ms 周期 ==========
        if (user1TaskInfo.taskCnt % 25 == 0) {
            // 1. 红外传感器读取
            // 2. RFID 读取
        }

        // ========== 100ms 周期 ==========
        if (user1TaskInfo.taskCnt % 50 == 0) {
            // 1. 处理 APP 命令（RX 命令分发，按 cmdSeq 去重）
            // 2. 更新报警状态（余量、卡堵、断电）
            // 3. 更新蜂鸣器
        }

        // ========== 500ms 周期 ==========
        if (user1TaskInfo.taskCnt % 250 == 0) {
            // 1. 计划执行检查（按时间触发投喂）
            // 2. 写入 TX 状态变量（重量、状态、报警等）
        }

        // ========== 1s 周期 ==========
        if (user1TaskInfo.taskCnt % 500 == 0) {
            // 1. 系统秒计数 ++
            // 2. 投喂计划列表广播（轮播下一条到 TX[12]~[15]）
            // 3. Flash 持久化触发（仅在计划/历史/阈值变更时写入，避免频繁擦写）
            // 4. 跨日检测：若进入新一天，feedCountToday 复位
        }

        osDelay(2);
    }
}
```

### 5. 关键状态机：投喂状态机

```
状态：FEED_STATE_IDLE（默认）
  触发条件 → 进入 FEEDING：
    A) 计划模式（planEnabled=1）下，currentPlanId 对应方案的 frequencySec 已到期
       且 currentWeightG < targetWeightG（食槽重量低于目标，需要补充）
    B) 手动模式（planEnabled=0）下，收到 USER1_CMD_SERVO_OPEN
  动作：
    - 记录 feedStartTickMs、feedStartWeightG
    - 调用舵机驱动顺时针旋转（开槽）
    - feedingState = FEEDING

状态：FEED_STATE_FEEDING
  退出条件 → 进入 IDLE：
    A) currentWeightG ≥ targetWeightG（达到目标重量，正常完成）
    B) 手动模式下收到 USER1_CMD_SERVO_CLOSE
    C) 卡堵检测触发（投喂时间 > 10s 且重量增加 < 1g，判定卡堵）
  动作：
    - 调用舵机驱动逆时针旋转（关槽）
    - 完成投喂时记录历史（actualWeightG = 当前重量 - feedStartWeightG）
    - 更新对应 plan 的 lastFeedTickMs
    - feedCountToday++
    - feedingState = IDLE
    - 标记 plans/history 需要持久化
```

**重量到达判定**：考虑舵机关闭和食物滑落延迟，实际策略为「当 currentWeightG ≥ targetWeightG - 10g 时触发关闭」，需求中提及"当重量达到预设值附近（比如 100g，则重量传感器反馈 90g 时）逆时针转动关闭食槽"。

### 6. 计划执行逻辑

每 500ms 检查一次：

```
若 planEnabled == 1 且 feedingState == IDLE：
    根据优先级确定 currentPlanId：
      - 若收到 APP 选择命令（USER1_CMD_PLAN_SELECT）→ 使用 APP 选定的 planId（最高优先级）
      - 否则若 rfidPetId != 0 且对应 planId 存在 → 使用 rfidPetId 对应方案
      - 否则保持原 currentPlanId（默认 1）

    取出 currentPlanId 对应 plan：
      若 (HAL_GetTick() - plan.lastFeedTickMs) ≥ plan.frequencySec * 1000：
          targetWeightG = currentWeightG + plan.weightG
          进入投喂状态
```

### 7. 报警逻辑（每 100ms 评估）

```
余量不足报警：
  若 currentWeightG < alarmThresholdG → 置位 ALARM_LOW_FOOD，蜂鸣器鸣响
  否则清除该位

卡堵报警：
  若 feedingState == FEEDING 且 (HAL_GetTick() - feedStartTickMs > 10000)
     且 (currentWeightG - feedStartWeightG < 1.0f)
  → 置位 ALARM_MOTOR_JAM，强制关闭舵机回 IDLE，蜂鸣器鸣响
  报警在收到任何 APP 命令或重新启动投喂后清除

断电报警：
  若 bleConnected == 0 → 置位 ALARM_POWER_OFF
  否则清除该位
  说明：bleConnected 通过 TX[0]~[3] 与 ESP32 私有通信获取（见下文）；
       系统断电报警仅作状态显示，不触发蜂鸣器（实际断电时 STM32 已不工作）

蜂鸣器规则：
  alarmFlags 任一位置位 → beeperOn = 1，调用蜂鸣器驱动鸣响（短促间歇音）
  alarmFlags 全部清零 → beeperOn = 0，关闭蜂鸣器
```

### 8. 投喂计划列表同步（向 APP 广播）

每 1s 切换一条方案到 TX[12]~[15]，APP 端按 `planSyncId` 字段更新本地计划缓存：

```c
/* 1s 周期内执行 */
uint8_t i = user1TaskInfo.planBroadcastIndex;
user1Plan_t *p = &user1TaskInfo.plans[i];

remoteInfo.remoteVar_TX[12].var_float  = p->frequencySec;
remoteInfo.remoteVar_TX[13].var_float  = p->weightG;
remoteInfo.remoteVar_TX[14].var_uint32 = p->planId;          /* 0 表示该槽位空 */
remoteInfo.remoteVar_TX[15].var_uint32 = USER1_PLAN_MAX_COUNT; /* 总方案数（5） */

user1TaskInfo.planBroadcastIndex = (i + 1) % USER1_PLAN_MAX_COUNT;
```

5 秒一轮完整广播完所有方案，APP 端自然同步。

### 9. APP 命令处理（每 100ms 检查 RX）

通过 `cmdSeq` 去重，同一 seq 不重复执行：

```c
uint8_t cmdSeq = (uint8_t)(remoteInfo.remoteVar_RX[6].var_uint32 & 0xFF);
if (cmdSeq == user1TaskInfo.lastCmdSeq) return;  /* 已处理 */

uint8_t cmdCode = (uint8_t)(remoteInfo.remoteVar_RX[2].var_uint32 & 0xFF);
uint8_t planId  = (uint8_t)(remoteInfo.remoteVar_RX[3].var_uint32 & 0xFF);
float   freqSec = remoteInfo.remoteVar_RX[0].var_float;
float   weightG = remoteInfo.remoteVar_RX[1].var_float;
float   thresh  = remoteInfo.remoteVar_RX[4].var_float;

switch (cmdCode) {
    case USER1_CMD_PLAN_SET:    /* 新增或修改 */
        /* 查找 planId 对应槽位，存在则改，否则找空槽位（planId=0）添加 */
        ...
        markPlansDirty();
        break;
    case USER1_CMD_PLAN_DEL:
        /* 找到 planId 对应槽位，置 planId=0 */
        ...
        markPlansDirty();
        break;
    case USER1_CMD_SERVO_OPEN:
        if (!user1TaskInfo.planEnabled) startManualFeed();
        break;
    case USER1_CMD_SERVO_CLOSE:
        if (!user1TaskInfo.planEnabled) stopManualFeed();
        break;
    case USER1_CMD_PLAN_ENABLE:
        user1TaskInfo.planEnabled = 1;
        markFlashDirty();
        break;
    case USER1_CMD_PLAN_DISABLE:
        user1TaskInfo.planEnabled = 0;
        markFlashDirty();
        break;
    case USER1_CMD_PLAN_SELECT:
        if (planExists(planId)) user1TaskInfo.currentPlanId = planId;
        break;
    case USER1_CMD_THRESHOLD_SET:
        if (thresh > 0.0f && thresh < 1000.0f) {
            user1TaskInfo.alarmThresholdG = thresh;
            markFlashDirty();
        }
        break;
}

user1TaskInfo.lastCmdSeq = cmdSeq;
```

### 10. Flash 持久化策略

- 标记 `dirty` 标志，1 秒周期内若有 dirty 则写一次 Flash
- Flash 中按区域存储：
  - 区域 A：`plans[]` 数组
  - 区域 B：`history[]` 环形缓冲 + head + count
  - 区域 C：配置（`alarmThresholdG`、`planEnabled`）
- 每个区域加魔数和简单校验（如 CRC8 或异或校验和），读取时校验失败则使用默认值

---

## 四、APP 通信变量布局

### TX（STM32 → APP）

> 仅 [4]~[15] 对 APP 可见。[0]~[3] 用于 STM32-ESP32 私有通信（接收 BLE 连接状态等）。

| 索引 | 有效宽度 | 变量含义 | 数据类型 | 说明 |
|---|---|---|---|---|
| [0] | 4B | (保留 - ESP32 私有) | — | ESP32 内部使用 |
| [1] | 4B | (保留 - ESP32 私有) | — | ESP32 内部使用 |
| [2] | 1B | bleConnected | uint8 | ESP32 写入：0=未连接，1=已连接（仅供 STM32 读取，APP 不可见） |
| [3] | 1B | (保留 - ESP32 私有) | — | ESP32 内部使用 |
| [4] | 4B | currentWeightG | float | 当前食槽重量（克） |
| [5] | 4B | alarmThresholdG | float | 当前报警阈值（克，回显） |
| [6] | 1B | feedingState | uint8 | 投喂状态：0=空闲，1=投喂中 |
| [7] | 1B | alarmFlags | uint8 | 报警位：bit0=余量不足, bit1=电机卡堵, bit2=外部断电 |
| [8] | 4B | feedCountToday | uint32 | 当日投喂次数 |
| [9] | 4B | systemSec | uint32 | 系统时间戳（秒，用于历史记录展示） |
| [10] | 1B | currentPlanId | uint8 | 当前执行方案ID（0=无，1~5） |
| [11] | 1B | planEnabled | uint8 | 计划执行开关：0=禁用，1=启用 |
| [12] | 4B | planSyncFreqSec | float | 计划广播-频率（秒） |
| [13] | 4B | planSyncWeightG | float | 计划广播-重量（克） |
| [14] | 1B | planSyncPlanId | uint8 | 计划广播-当前广播的方案ID（0=空槽位） |
| [15] | 1B | planSyncTotal | uint8 | 方案总数（固定 5，用于 APP 端缓冲数组长度） |

> 注：TX[2] 由 ESP32 固件填充 BLE 连接状态后转发给 STM32 的同步缓冲。如 ESP32 固件未实现该机制，可改为 STM32 通过其他方式（如心跳超时）判定，本字段可作为该判定结果。

### RX（APP → STM32）

| 索引 | 有效宽度 | 变量含义 | 数据类型 | 说明 |
|---|---|---|---|---|
| [0] | 4B | cmdParamFreqSec | float | 命令参数：投喂频率（秒） |
| [1] | 4B | cmdParamWeightG | float | 命令参数：投喂重量（克） |
| [2] | 1B | cmdCode | uint8 | 命令码（USER1_CMD_xxx） |
| [3] | 1B | cmdPlanId | uint8 | 命令目标方案ID（1~5） |
| [4] | 4B | cmdAlarmThresholdG | float | 设置报警阈值时使用 |
| [5] | 4B | (保留) | — | — |
| [6] | 1B | cmdSeq | uint8 | 命令序列号（每次新命令递增，用于去重） |
| [7] | 1B | (保留) | — | — |
| [8]~[15] | — | (保留) | — | 暂未使用，APP 端写 0 |

---

## 五、功能详细需求

### 功能 1：实时重量监测

- **描述**：每 10ms 读取称重传感器，更新 `currentWeightG`
- **实现**：调用称重传感器 driver 的读值 API，结果存入 `user1TaskInfo.currentWeightG`
- **关键参数**：10ms 周期（`taskCnt % 5 == 0`）

### 功能 2：自动按计划投喂

- **描述**：根据 `currentPlanId` 对应方案，按 `frequencySec` 周期触发投喂
- **实现**：500ms 周期检查计划是否到期，到期且空闲则启动投喂状态机
- **方案选择优先级**：APP 选择 > RFID 识别 > 默认（方案 1）
- **关键参数**：最多 5 个方案；预设时间与实际投喂误差 ≤ ±30 秒（500ms 周期检查可满足）

### 功能 3：手动投喂控制

- **描述**：APP 通过 `USER1_CMD_SERVO_OPEN/CLOSE` 远程开关舵机
- **限制**：仅在 `planEnabled == 0`（计划禁用）时有效
- **响应时间**：100ms 命令处理周期 + 舵机响应，≤ 3 秒（满足需求）

### 功能 4：投喂状态机（重量闭环）

- **描述**：舵机顺时针打开 → 称重达到目标 → 舵机逆时针关闭
- **关键参数**：目标重量回差 10g（`currentWeight ≥ target - 10g` 即关闭，对应需求中"100g 时反馈 90g 关闭"）
- **driver 调用**：舵机正转/反转 API、称重读值 API

### 功能 5：电机卡堵检测

- **描述**：投喂超过 10s 且重量未明显增加（< 1g），判定卡堵
- **响应**：强制关闭舵机，置位 `ALARM_MOTOR_JAM`，蜂鸣器鸣响

### 功能 6：余量报警

- **描述**：`currentWeightG < alarmThresholdG` 时报警
- **本地报警**：蜂鸣器鸣响（短促间歇）
- **远程报警**：`alarmFlags` 经 TX[7] 发给 APP，APP 端弹通知
- **场景准确性**：覆盖 ≤5g（满槽空）、半满 20~30g、满槽 ≥40g（实际值与检测值误差 ≤ ±3g 由称重传感器精度保证）

### 功能 7：报警阈值设置

- **描述**：APP 通过 `USER1_CMD_THRESHOLD_SET` + RX[4] 设置阈值
- **范围**：(0, 1000) 克
- **持久化**：写入 Flash

### 功能 8：投喂计划增删改

- **描述**：APP 通过 `USER1_CMD_PLAN_SET` / `USER1_CMD_PLAN_DEL` 修改计划列表
- **新增/修改逻辑**：按 RX[3] 指定的 planId 查找，存在则覆盖参数，不存在则找空槽位（planId=0）填入
- **删除逻辑**：找到 planId 对应槽位，置 planId=0
- **持久化**：每次修改后标记 dirty，1s 周期内写入 Flash
- **同步延迟**：APP → STM32 命令处理 ≤ 100ms，STM32 → APP 计划广播 1s 切换一次，整体同步 ≤ 5s（满足需求）
- **数据完整性**：cmdSeq 去重保证无丢失

### 功能 9：投喂计划列表广播

- **描述**：STM32 通过 TX[12]~[15] 循环广播 5 个槽位的方案，APP 据此显示和编辑
- **实现**：`planBroadcastIndex` 每秒递增取模

### 功能 10：历史记录管理

- **描述**：每次完成投喂记录到 `history[]` 环形缓冲（最多 30 条）
- **持久化**：写入 Flash，断电恢复
- **APP 查看**：APP 不需要实时查看完整历史；当前需求中"近 7 天投喂记录"由 APP 自行通过 `feedCountToday` 累积或采用其他方式实现，STM32 不主动推送完整列表（如后续要推送，使用与计划列表相同的轮播机制）

### 功能 11：RFID 个体识别

- **描述**：每 50ms 读取 RFID，识别到宠物则更新 `rfidPetId`
- **联动**：在计划模式下，若 APP 未主动选择方案，则使用 `rfidPetId` 对应的方案（planId == rfidPetId）
- **优先级**：APP 主动选择 > RFID 识别

### 功能 12：断电记忆

- **描述**：上电时从 Flash 恢复 `plans[]`、`history[]`、`alarmThresholdG`、`planEnabled`
- **恢复时间**：在 `user1TaskInit()` 中完成，启动后 ≤ 10 秒（满足需求）

### 功能 13：状态上报给 APP

- **描述**：500ms 周期写入 TX 状态变量
- **反馈延迟**：≤ 2 秒（实际 500ms 写入 + BLE 传输延迟，满足需求）

### 功能 14：BLE 连接监控（断电报警）

- **描述**：根据 ESP32 透传的连接状态判定。当 BLE 未连接时，置位 `ALARM_POWER_OFF`
- **实现方式**：方案 A — 通过 TX[2] 协调与 ESP32 通信，由 ESP32 写入连接状态（需 ESP32 固件配合）；方案 B — 若 ESP32 不支持，则通过命令心跳（APP 无命令超时 N 秒判定离线）。**实际实现 Claude Code 视 ESP32 接口情况选择**，并在实现文档中说明所选方案。

---

## 六、driver 层修改需求

**默认情况下不修改 driver 层**。仅在以下情况允许修改并必须在实现文档中明确记录：

- 称重传感器 driver 缺少滤波/校准 API → 可在 driver 层补充滤波（如均值滤波）
- 舵机 driver 缺少正反转控制 API → 可补充对应函数
- Flash driver 缺少分区读写 API → 可补充按地址读写的辅助函数

修改时必须遵守：
1. 接口命名风格：`DRIVER_XXX_FuncName(...)`
2. 公有声明放 .h，私有放 .c
3. 在 driver 文件顶部用注释标注修改点和修改原因
4. 所有可见状态通过公有结构体 `xxxInfo` 暴露

---

## 七、输出物要求

Claude Code 完成开发后必须输出：

### 1. 代码文件

- `userApp/task_user1.c` 和 `task_user1.h`
- 如果修改了 driver 层，输出修改后的 driver 文件，**在文件顶部用注释块标注修改点**，例如：

```c
/* === 修改记录 ===
 * 2026-XX-XX：增加 DRIVER_WEIGHT_GetFiltered() 函数，原因：均值滤波需求
 * === */
```

### 2. 功能实现文档（`implementation_report.md`）

包含以下章节：

#### (a) 功能实现状态清单

| 序号 | 功能描述 | 状态 | 说明 |
|---|---|---|---|
| 1 | 实时重量监测 | ✅/⚠️/❌ | |
| 2 | 自动按计划投喂 | ✅/⚠️/❌ | |
| 3 | 手动投喂控制 | ✅/⚠️/❌ | |
| 4 | 投喂状态机 | ✅/⚠️/❌ | |
| 5 | 电机卡堵检测 | ✅/⚠️/❌ | |
| 6 | 余量报警 | ✅/⚠️/❌ | |
| 7 | 报警阈值设置 | ✅/⚠️/❌ | |
| 8 | 投喂计划增删改 | ✅/⚠️/❌ | |
| 9 | 计划列表广播 | ✅/⚠️/❌ | |
| 10 | 历史记录管理 | ✅/⚠️/❌ | |
| 11 | RFID 识别 | ✅/⚠️/❌ | |
| 12 | 断电记忆 | ✅/⚠️/❌ | |
| 13 | 状态上报 | ✅/⚠️/❌ | |
| 14 | BLE 连接监控 | ✅/⚠️/❌ | |

状态：✅ 已实现，⚠️ 部分实现（说明缺什么），❌ 未实现（说明原因）

#### (b) driver 层修改记录（如有）

| 文件 | 修改类型 | 修改内容 | 原因 |
|---|---|---|---|

#### (c) 已实现功能的测试步骤

对每个功能给出测试方法和预期结果，例如：

> **功能 6（余量报警）测试**
> 1. 通过 APP 设置阈值为 30g
> 2. 在食槽中放入 50g 物品 → 蜂鸣器不响，TX[7] 的 bit0 = 0
> 3. 取出至 20g → 蜂鸣器开始鸣响，TX[7] 的 bit0 = 1，APP 端收到通知
> 4. 加入物品至 35g → 蜂鸣器停止，bit0 = 0

---

## 八、验证清单（开发完成后必须自检）

- [ ] `func_appcom.c/h` 未做任何改动
- [ ] `task_system.c/h` 未做任何改动
- [ ] `userLib/` 下未修改
- [ ] `Core/` 下未修改
- [ ] 代码中**没有出现** 0x17、0x18、0x19、0x1A、0x21、0x22、0x23、0x24 等 APP 端帧号
- [ ] 代码中**没有出现** `malloc` / `free` / `new` / `delete`
- [ ] 没有整体清零 `remoteVar_TX`（如 `memset(remoteVar_TX, 0, ...)`）
- [ ] 发送给 APP 的所有数据均使用 `remoteVar_TX[4]~[15]`，未占用 [0]~[3]
- [ ] 所有 `remoteVar_TX/RX` 索引访问均在 [0, 15] 范围内
- [ ] task 层未直接调用 HAL 函数
- [ ] task 层未做字节序处理（无 `__REV` / `htonl` / 手动字节交换）
- [ ] 所有公有结构体 / 函数声明在 `.h`，私有定义在 `.c`
- [ ] 命名风格符合：函数小驼峰、结构体 `xxxTaskInfo_t`、宏 `MODULE_CONSTANT`
- [ ] 注释为中文
- [ ] 编译通过，无警告
- [ ] 实现文档（`implementation_report.md`）已生成并填写完整
