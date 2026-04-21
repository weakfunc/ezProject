# STM32毕设开发框架说明文档

## 一、框架概述

本框架基于 STM32F103C8T6（HAL库 + FreeRTOS CMSIS_V2），采用分层架构，目标是让应用开发者只需专注于 **task层（应用层）** 的开发，即可完成具体项目功能。

框架已测试完毕的部分（不需要修改）：HAL层、stdlib层、driver层。
需要开发的部分：task层（以及根据需要扩展的func层）。

> **给应用开发者的重要说明**
>
> 开发task层时，**无需关心任何硬件连接、引脚分配或外设配置**。每个项目交付时，硬件已布线完毕，driver层已根据硬件配置实现并测试完成，task层只需调用driver层提供的API即可实现全部功能。具体当前项目可用哪些driver模块，由项目负责人在开始开发前告知，届时阅读对应driver的 `.h` 文件了解API即可。

---

## 二、分层架构

```
┌─────────────────────────────────┐
│         task层 (userApp)         │  ← 应用开发者编写，本文档重点
├─────────────────────────────────┤
│         func层 (userFunc)        │  ← 可选，跨driver的逻辑整合
├─────────────────────────────────┤
│        driver层 (userDriver)     │  ← 已完成，提供外设API
├─────────────────────────────────┤
│        stdlib层 (userLib)        │  ← 已完成，HAL封装
├─────────────────────────────────┤
│          HAL层 (Core/)           │  ← CubeMX生成，不可修改
└─────────────────────────────────┘
```

### 调用规则（严格遵守）

| 层级 | 可向下调用 |
|------|-----------|
| task层 | stdlib层、driver层、func层 |
| func层 | stdlib层、driver层 |
| driver层 | stdlib层（不可调用其他driver） |
| stdlib层 | HAL层 |

**禁止跨层调用**（task层不可直接调用HAL，driver层不可调用其他driver等）。

---

## 三、文件组织结构

```
MDK-ARM/
├── userLib/          # stdlib层
│   ├── stdlib_common.c/.h
│   ├── stdlib_usart.c/.h
│   ├── stdlib_tim.c/.h
│   ├── stdlib_i2c.c/.h
│   └── stdlib_dwt.c/.h
├── userDriver/       # driver层（按项目提供对应模块）
│   ├── driver_board.c/.h
│   ├── driver_xxx.c/.h
│   └── ...
├── userFunc/         # func层（可选）
│   └── func_xxx.c/.h
└── userApp/          # task层（应用开发区域）
    ├── task_system.c/.h
    └── task_user1.c/.h
```

---

## 四、driver层接口规范

### 4.1 每个driver模块向上提供的内容

每个driver模块有且仅有一个**公有信息结构体**，在.h文件中声明并extern：

```c
// .h 中
typedef struct {
    uint8_t  someState;
    uint16_t someValue;
    // ...
} xxxInfo_t;

extern xxxInfo_t xxxInfo;  // task层通过此结构体读取模块状态
```

多通道模块使用结构体数组：

```c
extern xxxInfo_t xxxInfo[XXX_CH_COUNT];  // 通过 xxxInfo[XXX_CH_0] 访问
```

### 4.2 driver的.h文件结构

每个driver的.h文件明确分为两部分：

```c
/*============================================================================
 * 向下依赖（driver依赖的stdlib资源，task层无需关心）
 *============================================================================*/
// 宏定义 driver 使用了哪些 stdlib 接口

/*============================================================================
 * 向上提供（task层使用的API）
 *============================================================================*/
// 公有宏、枚举、结构体、函数声明
```

### 4.3 driver层API使用方式

```c
// 初始化（在task_system的Init中调用一次）
DRIVER_XXX_Init();

// 读取状态（直接访问公有结构体）
xxxInfo.someState;

// 调用功能函数
DRIVER_XXX_DoSomething(param);

// 数据更新函数（部分driver需要在task循环中周期性调用）
DRIVER_XXX_Update();
```

---

## 五、task层开发规范

### 5.1 任务结构

每个任务文件包含：
1. 一个公有信息结构体（记录任务状态）
2. 一个初始化函数（Init，运行一次）
3. 一个更新函数（Updata，FreeRTOS任务入口，循环运行）

```c
// task_user1.h
typedef struct {
    uint32_t taskCnt;
    // 任务相关状态数据...
} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;

void user1TaskInit(void);
void user1TaskUpdata(void *argument);
```

```c
// task_user1.c
user1TaskInfo_t user1TaskInfo;

void user1TaskInit(void) {
    // 初始化所有用到的driver
    DRIVER_XXX_Init();
    DRIVER_YYY_Init();
    // 初始化任务内部状态
    user1TaskInfo.taskCnt = 0;
}

void user1TaskUpdata(void *argument) {
    user1TaskInit();  // 先执行初始化

    for(;;) {
        user1TaskInfo.taskCnt++;

        // 需要周期更新的driver
        DRIVER_XXX_Update();

        // 按倍数分频实现不同周期的逻辑
        if(user1TaskInfo.taskCnt % 10 == 0) {  // 每100ms（10 × 10ms基础周期）
            // 100ms周期逻辑
        }
        if(user1TaskInfo.taskCnt % 50 == 0) {  // 每500ms
            // 500ms周期逻辑
        }

        osDelay(10);  // 基础调度周期10ms
    }
}
```

### 5.2 task_system的作用

`task_system` 是系统任务，负责：
- 初始化所有stdlib层（由框架已实现，通常不需要修改）
- 在主循环中维持stdlib状态机运行（如 `STDLIB_USART_Updata()`）

应用开发者一般**不修改task_system**，在task_user1（或新增的task_userX）中完成业务逻辑。

### 5.3 新增任务

若需要多个并行任务，在 `freertos.c`（CubeMX USER CODE区域）中注册：

```c
/* USER CODE BEGIN RTOS_THREADS */
osThreadNew(user1TaskUpdata, NULL, &user1Task_attributes);
osThreadNew(user2TaskUpdata, NULL, &user2Task_attributes);
/* USER CODE END RTOS_THREADS */
```

---

## 六、编码约束

| 约束项 | 规则 |
|--------|------|
| 内存分配 | 禁止使用malloc，全部静态分配 |
| 注释语言 | 全部使用中文 |
| 首行缩进 | 代码正文首行缩进两个空格 |
| 私有定义 | 私有结构体、宏定义写在.c文件中 |
| 公有定义 | 对外结构体、宏定义写在.h文件中 |
| 命名风格 | 函数：`LAYER_MODULE_Action()`；结构体：`moduleXxx_t`；宏：`MODULE_CONSTANT` |
| 额外功能 | 不实现需求未提及的功能 |
| 边界检查 | driver层减少不必要的边界检查 |

---

## 七、命名规范速查

```
函数命名：
  STDLIB_USART_Init()        ← stdlib层
  DRIVER_BOARD_RgbOn()       ← driver层
  FUNC_APPCOM_Update()       ← func层
  user1TaskInit()            ← task层（小驼峰+TaskInit/TaskUpdata）

结构体命名：
  boardInfo_t / boardInfo    ← driver公有结构体（类型_t，实例无后缀）
  user1TaskInfo_t            ← task公有结构体

宏命名：
  BOARD_KEY1                 ← 模块常量
  WS2812_CH_0                ← 通道索引
  XXX_DEP_YYY                ← driver向下依赖的资源（仅在driver内部使用）
```

---

## 八、task层开发流程（给应用开发Claude的指引）

当收到具体项目需求时，按以下步骤规划task层：

1. **确认可用driver** - 当前项目已提供哪些driver模块（由用户告知）
2. **读取driver API** - 阅读对应driver的.h文件，了解向上提供的结构体和函数
3. **规划任务结构** - 确定需要几个FreeRTOS任务，各自职责
4. **设计公有结构体** - 定义每个task的Info结构体，包含需要跨任务共享的状态
5. **实现Init函数** - 调用所有需要的driver的Init函数，初始化任务状态
6. **实现Updata循环** - 用计数器分频实现不同周期的逻辑
7. **检查调用层级** - 确认没有跨层调用

### 示例：如果项目需要"读取按键控制LED"

```c
void user1TaskInit(void) {
    DRIVER_BOARD_Init();    // 初始化板载按键和LED
    DRIVER_WS2812_Init();   // 初始化WS2812彩灯
}

void user1TaskUpdata(void *argument) {
    user1TaskInit();
    for(;;) {
        user1TaskInfo.taskCnt++;

        DRIVER_BOARD_KeyInfoUpdate();  // 更新按键状态

        // 读取按键状态（通过公有结构体）
        if(boardInfo.key[BOARD_KEY1].isPressed) {
            DRIVER_WS2812_SetColor(WS2812_CH_0, 0, 255, 0, 0);  // 红色
        }

        osDelay(10);
    }
}
```

---

## 九、func层：appcom模块（上位机APP通信）

### 9.1 功能概述

`func_appcom` 是框架提供的上位机通信模块，已完整实现，task层**无需修改其内部逻辑**，只需读写 `remoteInfo` 结构体即可完成与APP的双向通信。

通信链路：
```
STM32 → USART1 → ESP32 → BLE → APP（发送方向）
APP   → BLE   → ESP32 → USART1 → STM32（接收方向）
```

### 9.2 变量类型：remoteVar_t

所有通信变量统一使用 `remoteVar_t` 类型，这是一个4字节联合体，支持三种类型解释：

```c
typedef union {
    uint8_t raw[4];
    struct __attribute__((packed)) {
        uint32_t var_uint32;  // 按无符号整型读写
        int      var_int32;   // 按有符号整型读写
        float    var_float;   // 按浮点型读写
    };
} remoteVar_t;
```

使用时选择匹配数据含义的字段访问即可，底层自动以原始字节传输：

```c
remoteInfo.remoteVar_TX[0].var_float   = 3.14f;   // 发送浮点数
remoteInfo.remoteVar_TX[1].var_uint32  = 1234U;   // 发送无符号整数
remoteInfo.remoteVar_TX[2].var_int32   = -50;     // 发送有符号整数
```

### 9.3 数据帧结构与容量

底层每帧 payload 固定 10 字节，含 **2个4字节槽位 + 2个1字节槽位**，共4个槽位：

```
每帧：[var_4b_1: 4B][var_4b_2: 4B][var_1b_1: 1B][var_1b_2: 1B]
```

共4帧（TX）/4帧（RX），总容量映射到 `remoteVar_TX[16]` / `remoteVar_RX[16]`：

| remoteVar索引 | 对应帧 | 对应槽位 | 有效宽度 |
|:---:|:---:|:---:|:---:|
| [0]  | 帧0 (0x09) | var_4b_1 | 4字节 |
| [1]  | 帧0 (0x09) | var_4b_2 | 4字节 |
| [2]  | 帧0 (0x09) | var_1b_1 | **1字节** |
| [3]  | 帧0 (0x09) | var_1b_2 | **1字节** |
| [4]  | 帧1 (0x0A) | var_4b_1 | 4字节 |
| [5]  | 帧1 (0x0A) | var_4b_2 | 4字节 |
| [6]  | 帧1 (0x0A) | var_1b_1 | **1字节** |
| [7]  | 帧1 (0x0A) | var_1b_2 | **1字节** |
| [8~11]  | 帧2 (0x0B) | 同上规律 | — |
| [12~15] | 帧3 (0x0C) | 同上规律 | — |

> 注意：索引 [2][3][6][7][10][11][14][15] 对应1字节槽位，写入时仅低8位有效，`var_float` / `var_int32` 不适用于这些位置。

RX方向（`remoteVar_RX[16]`）结构与TX完全对称，CMD范围为 0x13~0x16。

### 9.4 task层接口：remoteInfo

```c
typedef struct {
    uint8_t null;            // 占位符，未使用时填此字段

    // TX（STM32→APP）
    // 根据项目需求在此区域添加语义化字段，最终赋值给 remoteVar_TX[i]

    // RX（APP→STM32）
    uint8_t key1;            // APP按键1（已映射到 remoteVar_RX[2]）
    uint8_t key2;            // APP按键2（已映射到 remoteVar_RX[3]）
    uint8_t key3;            // APP按键3

    remoteVar_t remoteVar_TX[16];   // 发送变量池（task层写入）
    remoteVar_t remoteVar_RX[16];   // 接收变量池（task层读取）
} remoteInfo_t;

extern remoteInfo_t remoteInfo;
```

### 9.5 task层使用方式

task层**只操作 remoteInfo**，不直接接触底层帧结构，`FUNC_APPCOM_UPDATA()` 已由 `task_system` 周期性调用，无需手动触发：

```c
// ---- 发送数据到APP ----
remoteInfo.remoteVar_TX[0].var_float  = imuInfo.angle;       // 发送浮点角度
remoteInfo.remoteVar_TX[1].var_uint32 = systemTaskInfo.systemTaskCnt; // 发送计数
remoteInfo.remoteVar_TX[2].var_uint32 = motorSpeed;          // 1字节槽位，填uint32低8位

// ---- 从APP接收数据 ----
float    targetAngle = remoteInfo.remoteVar_RX[0].var_float;  // 读浮点目标值
uint32_t appCmd      = remoteInfo.remoteVar_RX[1].var_uint32; // 读无符号指令
uint8_t  appKey1     = remoteInfo.key1;                       // 读APP按键（1字节槽）
```

### 9.6 新项目接入checklist

1. **规划变量布局** - 列出需要TX/RX的变量及其类型（float/uint32/int32/uint8）
2. **分配索引** - 按9.3表格确认每个变量用哪个 `remoteVar_TX[i]` / `remoteVar_RX[i]`，注意1字节槽位限制
3. **扩展remoteInfo_t**（可选）- 在 `.h` 的TX/RX注释区添加语义化字段，提高可读性
4. **task层直接读写remoteInfo** - 无需任何其他操作

---

## 十、已配置外设（当前项目）

- USART1、USART2、USART3
- TIM2（CH1、CH2）、TIM3（CH1~CH4）、TIM4（CH1、CH4）
- GPIO、ADC、DMA、CAN

具体使用哪些外设，由已提供的driver模块决定，task层通过driver API访问，无需直接操作HAL。
