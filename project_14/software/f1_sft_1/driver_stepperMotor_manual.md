# driver_stepperMotor 使用手册

适用固件：ZDT X42S **X固件**，通讯接口 USART2（115200 baud，TTL）

---

## 目录

1. [模块概述](#1-模块概述)
2. [硬件配置确认](#2-硬件配置确认)
3. [初始化流程](#3-初始化流程)
4. [运动控制 API](#4-运动控制-api)
5. [状态读取 API](#5-状态读取-api)
6. [其他控制 API](#6-其他控制-api)
7. [反馈数据结构体](#7-反馈数据结构体)
8. [状态标志位说明](#8-状态标志位说明)
9. [完整使用示例](#9-完整使用示例)
10. [常见问题](#10-常见问题)

---

## 1. 模块概述

`driver_stepperMotor` 管理两个步进电机轴：

| 轴索引宏 | 值 | 说明 |
|---|---|---|
| `STEPPER_AXIS_PITCH` | 0 | 俯仰轴，电机地址 0x01 |
| `STEPPER_AXIS_YAW`   | 1 | 偏航轴，电机地址 0x02 |

所有 API 第一个参数均为轴索引，两轴共用 USART2 总线。

反馈数据通过 USART2 中断回调异步更新，结果存入全局结构体 `stepperMotorInfo[axis]`，上层直接读取。

---

## 2. 硬件配置确认

**使用前必须核对以下项，否则电机不响应：**

### 2.1 电机地址

电机地址通过电机驱动板上的拨码开关设置，出厂默认为 1。

| 电机 | 默认地址宏 | 宏定义位置 |
|---|---|---|
| 俯仰轴 | `STEPPER_ADDR_PITCH` = 0x01 | `driver_stepperMotor.h` |
| 偏航轴 | `STEPPER_ADDR_YAW`   = 0x02 | `driver_stepperMotor.h` |

> 如实际拨码与默认值不符，修改 `.h` 中对应宏即可。

### 2.2 串口配置

- **波特率**：115200
- **接口**：USART2（STM32 PA2/PA3）
- **电平**：TTL，需接到电机驱动板 TX/RX 引脚

### 2.3 首次使用必须做编码器校准

新电机或更换线序后，上位机执行一次 `Cal_MFL`（空载校准），否则位置精度会有 ±0.75°~±1.5° 误差。校准完成后掉电不丢失。

---

## 3. 初始化流程

### 3.1 API

```c
void DRIVER_STEPPER_Init(void);
```

功能：清零数据结构体，设置电机地址，注册 USART2 字节接收回调。

### 3.2 使能电机

```c
void DRIVER_STEPPER_Enable(uint8_t axis, uint8_t enable, uint8_t sync);
```

| 参数 | 说明 |
|---|---|
| `axis`   | 轴索引 |
| `enable` | 1 = 使能（锁轴）；0 = 失能（松轴，可手动转动）|
| `sync`   | `STEPPER_SYNC_NOW` 立即执行；`STEPPER_SYNC_WAIT` 缓存等待同步触发 |

### 3.3 典型 task 初始化写法

```c
void user1TaskInit(void) {
    DRIVER_STEPPER_Init();
    osDelay(50);                                                    // 等待 USART 稳定
    DRIVER_STEPPER_Enable(STEPPER_AXIS_PITCH, 1U, STEPPER_SYNC_NOW);
    osDelay(100);                                                   // 等待电机完成使能
}
```

---

## 4. 运动控制 API

### 4.1 速度模式（持续旋转）

```c
void DRIVER_STEPPER_SetSpeed(uint8_t axis, uint8_t dir,
                             uint16_t accel, uint16_t speed,
                             uint8_t sync);
```

| 参数 | 类型 | 范围 | 说明 |
|---|---|---|---|
| `dir`   | uint8_t  | `STEPPER_DIR_CW` / `STEPPER_DIR_CCW` | 旋转方向 |
| `accel` | uint16_t | 0 ~ 65535 | 加速度，单位 RPM/S |
| `speed` | uint16_t | 0 ~ 30000 | 速度，单位 **0.1RPM**（3000 = 300.0RPM）|
| `sync`  | uint8_t  | `STEPPER_SYNC_NOW` / `STEPPER_SYNC_WAIT` | 同步标志 |

**示例：CCW 方向，加速度 500RPM/S，速度 200.0RPM**
```c
DRIVER_STEPPER_SetSpeed(STEPPER_AXIS_PITCH, STEPPER_DIR_CCW, 500U, 2000U, STEPPER_SYNC_NOW);
```

> 发送停止命令 `DRIVER_STEPPER_Stop()` 才会停下，否则持续旋转。

---

### 4.2 直通限速位置模式（简单位置控制）

```c
void DRIVER_STEPPER_SetPosDirect(uint8_t axis, uint8_t dir,
                                 uint16_t speed, uint32_t pos,
                                 uint8_t mode, uint8_t sync);
```

| 参数 | 类型 | 范围 | 说明 |
|---|---|---|---|
| `dir`   | uint8_t  | `STEPPER_DIR_CW` / `STEPPER_DIR_CCW` | 运动方向 |
| `speed` | uint16_t | 0 ~ 30000 | 运行速度，单位 **0.1RPM** |
| `pos`   | uint32_t | 0 ~ 4294967295 | 角度，单位 **0.1°**（900 = 90.0°）|
| `mode`  | uint8_t  | 见下表 | 运动模式 |
| `sync`  | uint8_t  | `STEPPER_SYNC_NOW` / `STEPPER_SYNC_WAIT` | 同步标志 |

**运动模式（mode）：**

| 宏 | 值 | 说明 |
|---|---|---|
| `STEPPER_MODE_REL_LAST` | 0 | 相对上一目标位置（增量运动）|
| `STEPPER_MODE_ABS`      | 1 | 相对坐标零点的绝对位置（**常用**）|
| `STEPPER_MODE_REL_CUR`  | 2 | 相对当前实时位置（增量运动）|

**示例：CW 方向，1000RPM，绝对位置 90.0°**
```c
DRIVER_STEPPER_SetPosDirect(STEPPER_AXIS_PITCH, STEPPER_DIR_CW,
                             10000U, 900U,
                             STEPPER_MODE_ABS, STEPPER_SYNC_NOW);
```

---

### 4.3 梯形曲线加减速位置模式（推荐用于云台）

```c
void DRIVER_STEPPER_SetPosTrapezoid(uint8_t axis, uint8_t dir,
                                    uint16_t accAccel, uint16_t decAccel,
                                    uint16_t maxSpeed, uint32_t pos,
                                    uint8_t mode, uint8_t sync);
```

| 参数 | 类型 | 范围 | 说明 |
|---|---|---|---|
| `dir`      | uint8_t  | `STEPPER_DIR_CW` / `STEPPER_DIR_CCW` | 运动方向 |
| `accAccel` | uint16_t | 0 ~ 65535 | **加速**阶段加速度，单位 RPM/S |
| `decAccel` | uint16_t | 0 ~ 65535 | **减速**阶段加速度，单位 RPM/S |
| `maxSpeed` | uint16_t | 0 ~ 30000 | 最大速度，单位 **0.1RPM** |
| `pos`      | uint32_t | 0 ~ 4294967295 | 角度，单位 **0.1°** |
| `mode`     | uint8_t  | 同上 | 运动模式 |
| `sync`     | uint8_t  | `STEPPER_SYNC_NOW` / `STEPPER_SYNC_WAIT` | 同步标志 |

**示例：CW，加速300RPM/S，减速200RPM/S，最大速度500RPM，绝对位置180.0°**
```c
DRIVER_STEPPER_SetPosTrapezoid(STEPPER_AXIS_PITCH, STEPPER_DIR_CW,
                                3000U, 2000U, 5000U, 1800U,
                                STEPPER_MODE_ABS, STEPPER_SYNC_NOW);
```

> 相比 `SetPosDirect`，可单独控制加速和减速斜率，运动更平稳，适合云台场景。

---

### 4.4 力矩模式（电流控制）

```c
void DRIVER_STEPPER_SetTorque(uint8_t axis, uint8_t dir,
                              uint16_t slope, uint16_t current,
                              uint8_t sync);
```

| 参数 | 类型 | 范围 | 说明 |
|---|---|---|---|
| `dir`     | uint8_t  | `STEPPER_DIR_CW` / `STEPPER_DIR_CCW` | 电流方向 |
| `slope`   | uint16_t | 0 ~ 65535 | 电流爬升斜率，单位 mA/S |
| `current` | uint16_t | 0 ~ 5000  | 目标电流，单位 mA |
| `sync`    | uint8_t  | `STEPPER_SYNC_NOW` / `STEPPER_SYNC_WAIT` | 同步标志 |

**示例：CCW，斜率200mA/S，电流600mA**
```c
DRIVER_STEPPER_SetTorque(STEPPER_AXIS_PITCH, STEPPER_DIR_CCW, 200U, 600U, STEPPER_SYNC_NOW);
```

---

### 4.5 立即停止

```c
void DRIVER_STEPPER_Stop(uint8_t axis, uint8_t sync);
```

**示例：**
```c
DRIVER_STEPPER_Stop(STEPPER_AXIS_PITCH, STEPPER_SYNC_NOW);
```

---

## 5. 状态读取 API

以下函数只发送**请求帧**，电机回复后由中断回调自动解析并写入 `stepperMotorInfo[axis]`。

| 函数 | 功能码 | 更新字段 |
|---|---|---|
| `DRIVER_STEPPER_ReadPos(axis)`        | 0x36 | `realPos_01deg` |
| `DRIVER_STEPPER_ReadSpeed(axis)`      | 0x35 | `realSpd_01rpm` |
| `DRIVER_STEPPER_ReadEncoder(axis)`    | 0x31 | `encoder` |
| `DRIVER_STEPPER_ReadStatus(axis)`     | 0x3A | `motorStatus` |
| `DRIVER_STEPPER_ReadHomeStatus(axis)` | 0x3B | `homeStatus` |
| `DRIVER_STEPPER_ReadPosError(axis)`   | 0x37 | `posErr_001deg` |

**典型用法（50ms 任务中轮询）：**
```c
// 发送请求
DRIVER_STEPPER_ReadPos(STEPPER_AXIS_PITCH);

// 下一个周期读取结果（回复约 1ms 内到达）
int32_t pos = stepperMotorInfo[STEPPER_AXIS_PITCH].realPos_01deg;
float   deg = (float)pos / 10.0f;   // 换算为度
```

> **不要在同一个周期内连续发多条读命令**，两轴共用串口，每条命令间隔至少 2ms。

---

## 6. 其他控制 API

### 6.1 触发回零

```c
void DRIVER_STEPPER_GoHome(uint8_t axis, uint8_t homeMode, uint8_t sync);
```

| homeMode 值 | 说明 |
|---|---|
| 0 | 单圈就近回零（**最常用**，需提前 `DRIVER_STEPPER_ZeroPos` 设定过零点）|
| 1 | 单圈方向回零 |
| 2 | 无限位碰撞回零（电机撞到机械限位后退回，**云台常用**）|
| 3 | 限位开关回零 |
| 4 | 回到绝对坐标零点 |
| 5 | 回到上次掉电位置 |

**示例：无限位碰撞回零**
```c
DRIVER_STEPPER_GoHome(STEPPER_AXIS_PITCH, 2U, STEPPER_SYNC_NOW);
```

回零完成后电机主动返回 `地址 + 0x9A + 0x9F + 0x6B`，`lastAck` 字段更新为 `0x9F`（`STEPPER_ACK_DONE`）。

---

### 6.2 清零当前位置

```c
void DRIVER_STEPPER_ZeroPos(uint8_t axis);
```

将当前位置设为坐标零点，后续 `STEPPER_MODE_ABS` 绝对位置均相对此点。

---

### 6.3 解除保护

```c
void DRIVER_STEPPER_ClearError(uint8_t axis);
```

触发堵转/过热/过流保护后，电机停止响应运动命令，需调用此函数解除。

---

## 7. 反馈数据结构体

```c
typedef struct {
    uint8_t  addr;           // 电机串口地址
    int32_t  realPos_01deg;  // 实时位置，单位 0.1°，有符号（正=CW，负=CCW）
    int16_t  realSpd_01rpm;  // 实时转速，单位 0.1RPM，有符号
    uint16_t encoder;        // 线性化编码器值，0~65535 对应 0~360°（单圈绝对值）
    int32_t  posErr_001deg;  // 位置误差，单位 0.01°，有符号
    uint8_t  motorStatus;    // 电机状态标志（见第8节）
    uint8_t  homeStatus;     // 回零状态标志（见第8节）
    uint8_t  lastAck;        // 最近一次运动命令的应答码
} stepperMotorInfo_t;

extern stepperMotorInfo_t stepperMotorInfo[2];  // [0]=PITCH，[1]=YAW
```

**单位换算：**

```c
// 位置转换为度
float deg = stepperMotorInfo[STEPPER_AXIS_PITCH].realPos_01deg / 10.0f;

// 转速转换为 RPM
float rpm = stepperMotorInfo[STEPPER_AXIS_PITCH].realSpd_01rpm / 10.0f;

// 编码器转换为度（单圈绝对值）
float encDeg = stepperMotorInfo[STEPPER_AXIS_PITCH].encoder * 360.0f / 65536.0f;

// 位置误差转换为度
float errDeg = stepperMotorInfo[STEPPER_AXIS_PITCH].posErr_001deg / 100.0f;
```

---

## 8. 状态标志位说明

### 8.1 motorStatus 字段

| 宏 | 位 | 说明 |
|---|---|---|
| `STEPPER_STATUS_ENABLED` | bit0 | 1 = 电机已使能 |
| `STEPPER_STATUS_POS_OK`  | bit1 | 1 = 已到达目标位置 |
| `STEPPER_STATUS_JAM`     | bit2 | 1 = 堵转标志（未触发保护）|
| `STEPPER_STATUS_JAM_P`   | bit3 | 1 = 堵转保护已触发 |
| `STEPPER_STATUS_LIM_L`   | bit4 | 1 = 左限位开关触发 |
| `STEPPER_STATUS_LIM_R`   | bit5 | 1 = 右限位开关触发 |
| `STEPPER_STATUS_PWRLOSS` | bit7 | 1 = 发生过掉电 |

**示例：判断是否到位**
```c
if (stepperMotorInfo[STEPPER_AXIS_PITCH].motorStatus & STEPPER_STATUS_POS_OK) {
    // 电机已到达目标角度
}
```

### 8.2 homeStatus 字段

| 宏 | 位 | 说明 |
|---|---|---|
| `STEPPER_HOME_ENC_RDY` | bit0 | 1 = 编码器就绪（正常）|
| `STEPPER_HOME_CAL_RDY` | bit1 | 1 = 校准表就绪（已做过校准）|
| `STEPPER_HOME_IN_PROG` | bit2 | 1 = 正在回零 |
| `STEPPER_HOME_FAILED`  | bit3 | 1 = 回零失败 |
| `STEPPER_HOME_OTP`     | bit4 | 1 = 过热保护 |
| `STEPPER_HOME_OCP`     | bit5 | 1 = 过流保护 |

**示例：判断回零状态**
```c
uint8_t hs = stepperMotorInfo[STEPPER_AXIS_PITCH].homeStatus;
if      ((hs & 0x0CU) == 0x04U) { /* 正在回零 */ }
else if ((hs & 0x0CU) == 0x08U) { /* 回零失败 */ }
else if ((hs & 0x0CU) == 0x00U) { /* 回零完成/未启动 */ }
```

### 8.3 lastAck 字段

| 值 | 宏 | 说明 |
|---|---|---|
| 0x02 | `STEPPER_ACK_OK`        | 命令正确接收 |
| 0x9F | `STEPPER_ACK_DONE`      | 动作执行完成（到位/回零完成）|
| 0xE2 | `STEPPER_ACK_ERR_PARAM` | 参数错误或保护触发 |
| 0xEE | `STEPPER_ACK_ERR_FMT`   | 帧格式错误 |

---

## 9. 完整使用示例

### 9.1 单电机绝对位置运动

```c
/* task 初始化 */
void user1TaskInit(void) {
    DRIVER_STEPPER_Init();
    osDelay(50);
    DRIVER_STEPPER_Enable(STEPPER_AXIS_PITCH, 1U, STEPPER_SYNC_NOW);
    osDelay(100);

    /* 先回零，建立坐标系 */
    DRIVER_STEPPER_GoHome(STEPPER_AXIS_PITCH, 0U, STEPPER_SYNC_NOW);
    osDelay(2000);   /* 等待回零完成（视实际机构调整时间）*/

    /* 运动到绝对位置 90.0° */
    DRIVER_STEPPER_SetPosTrapezoid(STEPPER_AXIS_PITCH,
        STEPPER_DIR_CW, 500U, 300U, 5000U, 900U,
        STEPPER_MODE_ABS, STEPPER_SYNC_NOW);
}

/* task 循环（50ms 子任务）*/
if (user1TaskInfo.user1TaskCnt % 25 == 0) {
    DRIVER_STEPPER_ReadPos(STEPPER_AXIS_PITCH);
    DRIVER_STEPPER_ReadStatus(STEPPER_AXIS_PITCH);
}
```

### 9.2 检查应答码

```c
/* 发送使能命令后，下一周期检查应答 */
DRIVER_STEPPER_Enable(STEPPER_AXIS_PITCH, 1U, STEPPER_SYNC_NOW);
osDelay(5);
if (stepperMotorInfo[STEPPER_AXIS_PITCH].lastAck == STEPPER_ACK_OK) {
    /* 使能成功 */
}
```

### 9.3 处理堵转保护

```c
if (stepperMotorInfo[STEPPER_AXIS_PITCH].motorStatus & STEPPER_STATUS_JAM_P) {
    DRIVER_STEPPER_ClearError(STEPPER_AXIS_PITCH);   // 解除保护
    osDelay(10);
    DRIVER_STEPPER_Enable(STEPPER_AXIS_PITCH, 1U, STEPPER_SYNC_NOW);  // 重新使能
}
```

---

## 10. 常见问题

**Q: 发命令后电机无响应**
- 确认电机地址与 `STEPPER_ADDR_PITCH`/`STEPPER_ADDR_YAW` 一致（拨码开关）
- 确认 USART2 波特率为 115200
- 确认 TX/RX 接线无误（STM32 TX → 电机 RX，STM32 RX → 电机 TX）
- 用串口助手抓包确认帧是否正确发出

**Q: 位置不准**
- 检查 `homeStatus & STEPPER_HOME_CAL_RDY`，为 0 则需要做编码器校准
- 使用绝对模式前必须先回零

**Q: 电机振动/震颤**
- 降低 `accAccel`/`decAccel`，减小加速度
- 降低最大速度 `maxSpeed`
- 检查机械结构是否有共振

**Q: `lastAck` 始终是 0xE2**
- 参数超出范围（如速度超过 30000，电流超过 5000mA）
- 触发了堵转/过热/过流保护，需先调用 `DRIVER_STEPPER_ClearError()`

**Q: 读取位置始终为 0**
- 确认已调用 `DRIVER_STEPPER_ReadPos()` 且等待了至少 2ms 后再读 `realPos_01deg`
- 确认 USART2 RX 中断正常工作（`STDLIB_USART_SetCustomCb` 是否生效）
