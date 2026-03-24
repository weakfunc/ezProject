# driver_ws2812 模块说明

## 概述

WS2812B RGB LED 灯珠驱动模块，采用软件 bit-bang 方式驱动单总线协议，使用 Cortex-M3 DWT 硬件周期计数器精确控制时序。

- 控制接口：单 GPIO 输出，默认使用 `USER_IO_1`（PB8）
- 最大级联数：16 颗（`WS2812_LED_MAX_COUNT`）
- 时序精度：误差 ≤ 4 个 CPU 周期（约 55ns @ 72MHz），在 WS2812B ±150ns 容差内

---

## 文件结构

```
MDK-ARM/userDriver/driver_ws2812.c   // 驱动实现
MDK-ARM/userDriver/driver_ws2812.h   // 对外接口
```

---

## 依赖

| 依赖项 | 说明 |
|--------|------|
| `stdlib_common.h` | GPIO 操作、临界区 |
| Cortex-M3 DWT | 硬件周期计数器，由模块内部初始化，无需外部配置 |

---

## API 接口

### `DRIVER_WS2812_Init`

```c
void DRIVER_WS2812_Init(void);
```

初始化驱动，清空颜色缓存，同时启动 DWT 硬件周期计数器。**必须在其他 API 之前调用。**

---

### `DRIVER_WS2812_SetCtrlGpio`

```c
uint8_t DRIVER_WS2812_SetCtrlGpio(uint8_t gpioId);
```

设置数据输出引脚，成功返回 `1`，失败返回 `0`。

| 参数 | 说明 |
|------|------|
| `gpioId` | GPIO ID，取值见 `stdlib_common.h` 中的 `GPIO_ID_*` 宏 |

默认引脚为 `GPIO_ID_USER_IO_1`（PB8）。

---

### `DRIVER_WS2812_SetColor`

```c
void DRIVER_WS2812_SetColor(uint16_t ledId, uint8_t red, uint8_t green, uint8_t blue);
```

设置指定序号灯珠的颜色，仅写入缓存，需调用 `Refresh` 后生效。

| 参数 | 说明 |
|------|------|
| `ledId` | 灯珠序号，从 0 开始，最大 `WS2812_LED_MAX_COUNT - 1` |
| `red` / `green` / `blue` | 颜色分量，0~255 |

---

### `DRIVER_WS2812_SetAllColor`

```c
void DRIVER_WS2812_SetAllColor(uint8_t red, uint8_t green, uint8_t blue);
```

将所有灯珠缓存设为同一颜色，需调用 `Refresh` 后生效。

---

### `DRIVER_WS2812_Clear`

```c
void DRIVER_WS2812_Clear(void);
```

将所有灯珠缓存清零（全部熄灭），需调用 `Refresh` 后生效。

---

### `DRIVER_WS2812_Refresh`

```c
void DRIVER_WS2812_Refresh(uint16_t ledCount);
```

将缓存中前 `ledCount` 颗灯珠的颜色数据发送到灯带。调用期间会关闭全局中断以保证时序。

| 参数 | 说明 |
|------|------|
| `ledCount` | 本次刷新的灯珠数量，超出 `WS2812_LED_MAX_COUNT` 时自动截断 |

> **注意**：发送期间 CPU 被占用。8 颗灯珠约需 300µs，64 颗约需 2.4ms。

---

## 使用示例

```c
// 初始化
DRIVER_WS2812_Init();
DRIVER_WS2812_SetCtrlGpio(GPIO_ID_USER_IO_1);

// 点亮前8颗为红色
DRIVER_WS2812_SetAllColor(255, 0, 0);
DRIVER_WS2812_Refresh(8);

// 单独设置第0颗为蓝色
DRIVER_WS2812_SetColor(0, 0, 0, 255);
DRIVER_WS2812_Refresh(8);

// 全部熄灭
DRIVER_WS2812_Clear();
DRIVER_WS2812_Refresh(8);
```

---

## 时序说明

WS2812B 使用单线归零码（NRZ），协议帧格式为 GRB 顺序，每颗灯珠 24bit。

### 时隙参数（72MHz，当前实测校准值）

| 参数 | 周期数 | 时间 | WS2812B 规格 |
|------|--------|------|--------------|
| T0H（0-bit 高电平）| 20 | ~278ns | 250~550ns |
| T0L（0-bit 低电平）| 61 | ~847ns | 700~1000ns |
| T1H（1-bit 高电平）| 58 | ~806ns | 650~950ns |
| T1L（1-bit 低电平）| 32 | ~444ns | 300~600ns |
| Reset（复位低电平）| 3600 | ~50µs | >50µs |

### 时序实现原理

```
写 BSRR（拉高）→ 读 DWT->CYCCNT 作为起点 → 自旋等待 N 周期 → 写 BRR（拉低）→ 同上
```

DWT 计数器在 `DRIVER_WS2812_Init` 中启用，GPIO 写完寄存器后立即开始计时，不受函数调用开销影响。

### 时序参数调整

如需在不同主频或不同批次芯片上重新校准，修改 `driver_ws2812.c` 开头的以下常量（每单位 ≈ 1/SystemCoreClock）：

```c
static const uint32_t ws2812Bit0HighCyc = 20U;
static const uint32_t ws2812Bit0LowCyc  = 61U;
static const uint32_t ws2812Bit1HighCyc = 58U;
static const uint32_t ws2812Bit1LowCyc  = 32U;
static const uint32_t ws2812ResetCyc    = 3600U;
```

建议用逻辑分析仪抓取数据线波形，分别测量 T0H、T0L、T1H、T1L，对照上表规格范围进行微调。若出现颜色全白（0-bit 被误读为 1-bit），应减小 `ws2812Bit0HighCyc`。
