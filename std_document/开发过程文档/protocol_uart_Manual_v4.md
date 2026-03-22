# STM32 串口通信协议模块 — stdlib_usart 说明文档

> 目标平台: STM32F103C8T6 | HAL库 | FreeRTOS (CMSIS_V2)
> 版本: 4.0

---

| 版本 | 时间     | 概述                 |
| ---- | -------- | -------------------- |
| V1.0 | 2026.3.6 | 初代版本             |
| V2.0 | 2026.3.7 | 新增串口管理用户配置 |
| V3.0 | 2026.3.8 | 新增收发模式配置，DMA Circular接收，自定义协议回调 |
| V4.0 | 2026.3.8 | 接口重构：HAL细节全部内聚到模块内部，对外仅暴露串口ID宏和最小API |

---

## 1. 概述

stdlib_usart 是一个轻量级串口通信协议模块，专为 STM32 HAL 库开发环境设计。模块采用环形缓冲区 + 状态机架构，将接收与协议解析解耦，具备帧同步自恢复能力。

V4.0 的核心变化是**接口重构**：所有 HAL 句柄（`huart1` 等）、协议配置结构体、收发模式宏、内部类型定义全部隐藏在 `.c` 文件内部，头文件只暴露三个串口ID宏和五个API函数。调用方不需要 `#include "usart.h"`，不需要知道底层是 DMA 还是中断，不需要接触任何 HAL 类型。

核心特性：

- 对外接口仅串口ID宏 + 五个函数，零 HAL 依赖
- 三种协议模式：可变帧长（CMD+LEN+CRC+TAIL）、固定帧长（仅帧头+数据）、自定义协议（外部回调）
- USART1/2 使用 DMA Circular 接收 + DMA Normal 发送，USART3 使用中断收发
- CRC8 和帧尾校验可独立启用/禁用
- 50ms 帧接收超时，错误自动恢复（ORE/FE/NE）
- 全部静态内存分配

---

## 2. 对外接口总览

### 2.1 串口ID宏

```c
#define UART_PORT1      0    /* USART1: DMA收发，标准协议 */
#define UART_PORT2      1    /* USART2: DMA收发，固定帧长 */
#define UART_PORT3      2    /* USART3: 中断收发，自定义协议 */
```

所有API的 `port` 参数均使用这些宏，调用方无需接触 `huart1`、`UART_HandleTypeDef` 等 HAL 类型。

### 2.2 API 列表

| 函数 | 用途 | 适用模式 |
|------|------|---------|
| `uartInit()` | 初始化所有串口并启动接收 | 全部 |
| `uartUpdata()` | 主循环周期调用，驱动DMA搬运和状态机 | 全部 |
| `uartSetCustomCb(port, cb)` | 注册自定义协议回调（Init之前调用） | 自定义协议 |
| `uartGetFrame(port, &cmd, data, &len)` | 获取解析完成的帧 | 标准/固定帧长 |
| `uartSendFrame(port, cmd, data, len)` | 按内置协议打包发送 | 标准/固定帧长 |
| `uartSendRaw(port, data, len)` | 发送原始字节 | 自定义协议 |

---

## 3. 帧格式

### 3.1 可变帧长模式（UART_PORT1）

| SOF1 | SOF2 | CMD | LEN | DATA | CRC8 | TAIL |
|------|------|-----|-----|------|------|------|
| 0xAA | 0x55 | 1字节 | 1字节 | 0–32字节 | 1字节 | 0x0D |

CRC8 校验范围从 SOF1 到 DATA 末尾，多项式 0x07，初始值 0x00。`useCrc=0` 时 CRC 字节作为数据追加输出，`useTailCheck=0` 时帧尾字节作为数据追加输出。

### 3.2 固定帧长模式（UART_PORT2）

| SOF1 | SOF2 | DATA_0 | DATA_1 | ... | DATA_7 |
|------|------|--------|--------|-----|--------|
| 0xAA | 0x55 | 1字节 | 1字节 | ... | 1字节 |

仅校验帧头，帧头后8字节全部为数据。`GetFrame` 输出的 cmd 固定为 0。

### 3.3 自定义协议模式（UART_PORT3）

模块只负责底层收发（环形缓冲区、DMA/中断管理、错误恢复），每收到一个字节通过回调交给外部处理。发送使用 `uartSendRaw` 发送已打包的原始字节。

---

## 4. 内部架构

### 4.1 文件结构

| 文件 | 内容 |
|------|------|
| stdlib_usart.h | 串口ID宏、回调类型、API声明。**不包含**任何 HAL 类型、配置结构体、内部宏 |
| stdlib_usart.c | 全部内部实现：HAL句柄绑定、协议配置、收发模式、环形缓冲区、状态机、CRC8、HAL回调 |

### 4.2 数据流

模块采用三层解耦架构，接收层行为根据硬件模式自动切换：

**DMA模式（USART1/2）**：`HAL_UART_Receive_DMA` 以 Circular 模式持续写入 `dmaBuf`，`uartUpdata()` 通过 `__HAL_DMA_GET_COUNTER()` 计算写入位置，将新字节搬入环形缓冲区。无需 IDLE 中断。

**中断模式（USART3）**：`HAL_UART_RxCpltCallback` 每收到1字节压入环形缓冲区，立即重新挂起中断接收。

两种模式的数据最终都进入同一个环形缓冲区，解析层和应用层完全不受接收模式影响。`uartUpdata()` 从环形缓冲区逐字节取出，标准协议模式喂入内置状态机，自定义协议模式交给外部回调。

### 4.3 收发模式（硬件固定）

| 串口 | 接收 | 发送 | 修改方式 |
|------|------|------|---------|
| USART1 | DMA Circular | DMA Normal | 改 .c 内 `USART1_RX_DMA` / `USART1_TX_DMA` 宏，同步改 CubeMX |
| USART2 | DMA Circular | DMA Normal | 同上 |
| USART3 | 中断 | 中断 | 同上 |

### 4.4 状态机转移（标准协议模式）

| 当前状态 | 转移条件 | 下一状态 |
|----------|----------|----------|
| WAIT_SOF1 | 收到 sof1 | WAIT_SOF2 |
| WAIT_SOF2 | 收到 sof2 | WAIT_CMD |
| WAIT_SOF2 | 收到 sof1（非 sof2） | WAIT_SOF2（当作新帧头） |
| WAIT_SOF2 | 收到其他字节 | WAIT_SOF1（复位） |
| WAIT_CMD（可变帧长） | 任意字节 | WAIT_LEN |
| WAIT_CMD（固定帧长） | 任意字节 | RECV_DATA（作为data[0]） |
| WAIT_LEN | 0 ≤ byte ≤ 32 | RECV_DATA 或 WAIT_CRC8（LEN=0） |
| WAIT_LEN | byte > 32 | WAIT_SOF1（复位） |
| RECV_DATA | 收够 LEN/fixedLenValue 个字节 | WAIT_CRC8 或帧完成（固定帧长） |
| WAIT_CRC8 | useCrc=1 且匹配 | WAIT_TAIL |
| WAIT_CRC8 | useCrc=1 且不匹配 | WAIT_SOF1（复位） |
| WAIT_CRC8 | useCrc=0 | WAIT_TAIL（CRC字节追加到数据） |
| WAIT_TAIL | useTailCheck=1 且匹配 | WAIT_SOF1（帧成功） |
| WAIT_TAIL | useTailCheck=0 | WAIT_SOF1（帧尾追加到数据，帧成功） |

---

## 5. API 详细说明

### 5.1 uartInit

```c
void uartInit(void);
```

初始化所有串口。内部完成：串口ID与HAL句柄绑定、写入预置协议配置、清零状态、根据收发模式启动 DMA 或中断接收。在外设初始化之后、主循环之前调用一次。如需注册自定义回调，必须在 `uartInit` 之前调用 `uartSetCustomCb`。

### 5.2 uartUpdata

```c
void uartUpdata(void);
```

主循环周期调用。遍历所有串口实例，依次执行：DMA数据搬运（仅DMA模式串口）、帧接收超时检测（仅标准协议模式）、排空环形缓冲区并喂入状态机或回调。建议调用周期不超过 10ms。

### 5.3 uartSetCustomCb

```c
void uartSetCustomCb(uint8_t port, uartCustomParseCb_t cb);
```

注册自定义协议解包回调。回调签名为 `void cb(uint8_t port, uint8_t byte)`，每收到一个字节调用一次。必须在 `uartInit` 之前调用。

### 5.4 uartGetFrame

```c
uint8_t uartGetFrame(uint8_t port, uint8_t *cmd, uint8_t *data, uint8_t *len);
```

获取标准协议/固定帧长模式下解析完成的帧。自定义协议模式的串口调用此函数始终返回 0。

| 参数 | 说明 |
|------|------|
| port | `UART_PORTx` |
| cmd | 输出：命令字节（固定帧长模式下为 0） |
| data | 输出：数据缓冲区，至少 32 字节 |
| len | 输出：实际数据长度 |
| 返回值 | 1=有新帧，0=无 |

### 5.5 uartSendFrame

```c
void uartSendFrame(uint8_t port, uint8_t cmd, uint8_t *data, uint8_t len);
```

按内置协议格式打包并发送。可变帧长模式组装 SOF1+SOF2+CMD+LEN+DATA+CRC8+TAIL，固定帧长模式组装 SOF1+SOF2+DATA（cmd忽略）。自定义协议模式的串口调用此函数无效果。

### 5.6 uartSendRaw

```c
void uartSendRaw(uint8_t port, uint8_t *data, uint16_t len);
```

发送已打包的原始字节，供自定义协议模式使用。外部自行完成打包后调用此函数发送。标准协议模式的串口也可调用此函数绕过内置打包。

> 注意：`uartSendRaw` 直接发送传入的 `data` 指针指向的内存。DMA模式下该缓冲区在发送完成前不能被修改，调用方需确保缓冲区生命周期安全（使用全局或 static 数组）。

---

## 6. 使用示例

### 6.1 裸机环境

```c
#include "stdlib_usart.h"

extern void myParseByte(uint8_t port, uint8_t byte);  // 外部实现

/* USER CODE BEGIN 2 */
uartSetCustomCb(UART_PORT3, myParseByte);
uartInit();
/* USER CODE END 2 */

/* USER CODE BEGIN 3 */
uartUpdata();

uint8_t cmd, len, rxData[32];
if (uartGetFrame(UART_PORT1, &cmd, rxData, &len)) {
    switch (cmd) {
        case 0x01: /* ... */ break;
        case 0x02: /* ... */ break;
    }
}
if (uartGetFrame(UART_PORT2, &cmd, rxData, &len)) {
    // 固定帧长: cmd=0, rxData[0]~rxData[7]
}
/* USER CODE END 3 */
```

### 6.2 FreeRTOS 环境

```c
void UartTask(void *argument) {
    uartSetCustomCb(UART_PORT3, myParseByte);
    uartInit();
    uint8_t cmd, len, rxData[32];

    for (;;) {
        uartUpdata();
        if (uartGetFrame(UART_PORT1, &cmd, rxData, &len)) { /* ... */ }
        if (uartGetFrame(UART_PORT2, &cmd, rxData, &len)) { /* ... */ }
        osDelay(5);
    }
}
```

### 6.3 自定义协议实现（单独的 .c 文件）

```c
/* my_protocol.c */
#include "stdlib_usart.h"

void myParseByte(uint8_t port, uint8_t byte)
{
    // 你自己的解包状态机或协议逻辑
}

void mySendPacket(uint8_t *payload, uint8_t len)
{
    static uint8_t buf[64];
    // 你自己的打包逻辑
    uartSendRaw(UART_PORT3, buf, len);
}
```

---

## 7. 内部配置修改指南

所有配置都在 `stdlib_usart.c` 内部，无需改头文件。

### 7.1 修改协议参数

修改 `cfgPreset` 数组中对应串口的字段：

```c
static const protocolCfg_t cfgPreset[PORT_MAX] = {
    [UART_PORT1] = { .sof1=0xAA, .sof2=0x55, .tail=0x0D, .useCrc=1, ... },
    [UART_PORT2] = { ... },
    [UART_PORT3] = { .customProtocol=1 },
};
```

### 7.2 修改收发模式

修改 `.c` 内的模式宏，同时在 CubeMX 中同步修改 DMA 配置：

```c
#define USART1_RX_DMA   1   // 改为0则切换为中断接收
#define USART1_TX_DMA   1   // 改为0则切换为中断发送
```

### 7.3 增减串口

修改 `.h` 中的串口ID宏和 `.c` 中的 `PORT_MAX`、`portMap` 绑定、`cfgPreset` 数组、`isRxDma`/`isTxDma` 函数。

### 7.4 调整缓冲区大小

修改 .h 内的宏：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| DATA_MAX_LEN | 32 | DATA区最大字节数 |
| RX_BUF_SIZE | 64 | 环形缓冲区大小，必须为2的幂 |
| DMA_BUF_SIZE | 128 | DMA接收缓冲区大小 |
| TIMEOUT_MS | 50 | 帧接收超时（ms） |

---

## 8. 异常处理

- **字节丢失/帧错位**: 状态机任何阶段不符合预期立即复位，下一个有效帧头自动恢复同步
- **数据区伪帧头**: RECV_DATA 状态下不触发帧头检测
- **接收超时**: 50ms 无新字节自动复位状态机
- **缓冲区溢出**: 满时新字节丢弃，不覆盖未处理数据
- **连续帧头**: 0xAA 0xAA 0x55 时最后一个 0xAA 作为新帧头起点
- **DMA回绕**: `uartUpdata` 内部通过取模运算正确处理跨边界数据
- **HAL错误恢复**: `HAL_UART_ErrorCallback` 清除 ORE/FE/NE 标志并重新启动接收
- **发送忙等待**: DMA/中断发送前检查 BUSY_TX，确保上一帧发完再启动

---

## 9. SRAM 占用

每个串口实例约占：64（环形缓冲区）+ 128（DMA缓冲区）+ 32（解析缓存）+ 32（输出缓存）+ 38（发送缓冲区）+ 控制变量 ≈ **320 字节**。三路串口合计约 **960 字节**。

---

## 10. 注意事项

1. **环形缓冲区大小必须为 2 的幂**，内部使用位与运算代替取模。
2. **HAL回调归属**: `HAL_UART_RxCpltCallback` 和 `HAL_UART_ErrorCallback` 已在模块内实现，工程中不能重复定义。其他模块需要这些回调时需手动合并。
3. **DMA CubeMX配置**: 接收DMA必须 Circular 模式，发送DMA必须 Normal 模式。
4. **uartSendRaw 缓冲区安全**: DMA模式下传入的缓冲区在发送完成前不能修改，必须用全局或 static 数组。
5. **uartSetCustomCb 必须在 uartInit 之前调用**，否则回调不会被写入实例。
6. **自定义协议模式下 uartGetFrame 和 uartSendFrame 无效**，分别返回0和直接返回。
7. **头文件中的内部定义**: 当前版本中状态机枚举、配置结构体、实例结构体仍保留在头文件中以便调试器观察。如需完全隐藏可将它们移入 `.c` 文件并在 `.h` 中使用不透明指针。
