# 实现报告 — 红外非接触测温报警仪

## (a) 功能实现状态清单

| 序号 | 功能描述 | 状态 | 说明 |
|------|----------|------|------|
| 1 | 红外温度周期采集（100ms） | ✅ | `taskCnt%50==0` 调用 `DRIVER_GY615_Request()` + `DRIVER_GY615_GetInfo()`，有效性校验 -20~120°C |
| 2 | KEY1 刷新 OLED 温度显示 | ✅ | 边沿检测 pressCount 变化，更新 `tempDisplay = tempLatest`，重置 displayHoldCnt |
| 3 | KEY2 阈值+0.1°C | ✅ | `alarmThreshHighX10++`，限幅到 600（60.0°C） |
| 4 | KEY3 阈值-0.1°C | ✅ | `alarmThreshHighX10--`，限幅到 0（0.0°C） |
| 5 | 超阈值蜂鸣器报警 | ✅ | 超上限时设 `boardInfo.buzzTimeMs = 0xFFFF`，由 system 任务 BuzzUpdate 驱动 |
| 6 | 超阈值 RGB 红色报警 | ✅ | `DRIVER_BOARD_RgbSet(BOARD_RGB_R, 1)` |
| 7 | OLED 显示温度/阈值/报警提示 | ✅ | 4行布局：Temp / Alm / ALARM状态 / 电量图标 |
| 8 | OLED 显示电池电量图标 | ✅ | 5级文字图标 `[====]`~`[    ]`，低电量闪烁 |
| 9 | ADC1 电池电压采集换算百分比 | ✅ | driver_adc 读取 stdlib_adc 已采样结果（system任务每500ms采样），线性换算 3.3V=0%~4.2V=100% |
| 10 | 显示保持 10 秒 | ✅ | `displayHoldCnt` 每2ms计数，不主动清屏，tempDisplay 持续显示 |
| 11 | 30 秒无操作自动休眠 | ✅ | `idleCnt` 每2ms计数，达15000后关OLED/蜂鸣器/RGB，休眠中每轮调用 `DRIVER_POWER_EnterSleep()` |

## (b) driver 层修改记录

| 文件 | 修改类型 | 修改内容 | 原因 |
|------|----------|----------|------|
| `driver_adc.h` | 新增 | ADC电池电量驱动头文件 | 框架无ADC driver层封装，task层需通过driver层访问ADC数据 |
| `driver_adc.c` | 新增 | 从 adcInfo.battVoltage 换算电量百分比；不重复调用 STDLIB_ADC_Sample（system任务已处理） | 同上 |
| `driver_power.h` | 新增 | 低功耗管理驱动头文件 | task层禁止直接调用 HAL_PWR，需driver层封装 |
| `driver_power.c` | 新增 | `DRIVER_POWER_EnterSleep()` 调用 `HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI)` | 同上 |

## (c) 功能测试步骤

### 1. 温度采集验证
- 上电后等待 200ms（两次 100ms 采集周期）
- 用手接触 GY615 传感器前端，按 KEY1
- OLED 第1行应显示约 33~36°C 的体表温度（取决于测试距离）
- 移开手后再按 KEY1，温度应下降

### 2. KEY2/KEY3 阈值调整验证
- 连按 KEY2 三次，OLED 第2行阈值应从 "37.5C" 变为 "37.8C"
- 连按 KEY3 三次，阈值应回到 "37.5C"
- 阈值边界测试：调整到 0.0C 后再按 KEY3，应保持在 0.0C；调整到 60.0C 后再按 KEY2，应保持在 60.0C

### 3. 报警触发验证
- 将阈值 KEY3 向下调至低于当前测量温度（如从 37.5 调至 30.0°C）
- 蜂鸣器应开始鸣叫，OLED 第3行显示 "**ALARM!**"，RGB 红灯亮起
- 将阈值 KEY2 调回正常范围，报警应停止，OLED 第3行恢复空白

### 4. 电量图标验证
- 上电后 OLED 第4行应显示 "Bat:[====] XX%"（根据实际电池电压）
- 可用外部电源调节输入电压（需通过分压网络），验证图标档位变化：
  - > 75%: `[====]`，50-74%: `[=== ]`，25-49%: `[==  ]`，10-24%: `[=   ]`，< 10%: 闪烁

### 5. 休眠验证
- 上电后静置 30 秒不操作
- OLED 应熄灭（DisplayOff），蜂鸣器和 RGB 灯均关闭
- 按任意按键（KEY1/2/3），OLED 应重新点亮并显示上次内容，系统正常运行

### 6. 显示保持验证
- 按 KEY1 刷新温度后，至少等待 10 秒
- 期间 OLED 应持续显示按键时的温度值，不自动清除

## 设计说明

### 阈值整数域实现
为避免 float 加减累积误差，报警阈值使用 `int16_t alarmThreshHighX10`（单位 0.1°C）存储，KEY2/KEY3 操作为整数加减，显示时做 `/10.0f` 换算。

### 按键边沿检测
通过比较 `boardInfo.key[n].pressCount` 与上次快照检测新按下事件，`pressCount` 由 system 任务（每 10ms）的 `DRIVER_BOARD_KeyInfoUpdate()` 更新，无需在 user 任务重复调用。

### ADC 不重复采样
`STDLIB_ADC_Sample()` 已在 system 任务每 500ms 调用，`driver_adc` 仅读取 `adcInfo.battVoltage` 结果，不在 user 任务额外触发采样，避免多任务竞态。

### 休眠实现
休眠时每轮 `DRIVER_POWER_EnterSleep()` 调用一次 WFI，处理器在 FreeRTOS SysTick（1ms）到来时唤醒，继续执行调度。按键事件由 system 任务持续处理，user 任务在下一个2ms周期检测 pressCount 变化后唤醒系统（OLED 重新开启、清 sleepFlag）。

### 初始温度有效性
`hasValidTemp` 标志防止首次采集前（tempLatest=0.0f）触发虚假报警（0.0 < 35.0°C 的下限条件）。
