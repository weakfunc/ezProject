# STM32 宠物智能饲喂系统实现报告

## 功能实现状态清单

| 序号 | 功能描述 | 状态 | 说明 |
|---|---|---|---|
| 1 | 实时重量监测 | ✅ | `task_user1` 每 10ms 调用 HX711 驱动，更新 `user1TaskInfo.currentWeightG`。 |
| 2 | 自动按计划投喂 | ✅ | 支持最多 5 个计划，500ms 周期检查 `frequencySec` 到期后触发投喂。 |
| 3 | 手动投喂控制 | ✅ | APP 命令可在计划禁用时手动开/关舵机。 |
| 4 | 投喂状态机 | ✅ | 空闲/投喂中两态，达到目标重量前 10g 关闭，避免食物滑落延迟。 |
| 5 | 电机卡堵检测 | ✅ | 投喂超过 10s 且重量增长小于 1g 时置位报警并强制关槽。 |
| 6 | 余量报警 | ✅ | `currentWeightG < alarmThresholdG` 时置位低余量报警并请求蜂鸣器短鸣。 |
| 7 | 报警阈值设置 | ⚠️ | 运行时设置已实现；因现有 Flash 存储结构不足，暂未断电保存。 |
| 8 | 投喂计划增删改 | ⚠️ | 运行时增删改已实现；因现有 Flash 存储结构不足，暂未断电保存。 |
| 9 | 计划列表广播 | ✅ | 每 1s 轮播一个计划到 `remoteVar_TX[12]~[15]`。 |
| 10 | 历史记录管理 | ⚠️ | RAM 环形缓冲已实现，最多 30 条；暂未断电保存和完整推送。 |
| 11 | RFID 识别 | ⚠️ | 当前工程没有 RFID driver，保留 `rfidPetId` 字段和 50ms 占位。 |
| 12 | 断电记忆 | ⚠️ | `stdlib_flash` 当前只保存 `version`，无法保存计划/历史/配置；未修改底层。 |
| 13 | 状态上报 | ✅ | 每 500ms 写入 `remoteVar_TX[4]~[11]`。 |
| 14 | BLE 连接监控 | ⚠️ | 当前无 ESP32 私有连接状态输入，使用 APP 命令序列号心跳超时方案。 |

## Driver 层修改记录

| 文件 | 修改类型 | 修改内容 | 原因 |
|---|---|---|---|
| `MDK-ARM/userDriver/driver_senser.h` | 新增接口 | 增加 `senserInfo_t`、HX711 引脚宏、读数/校准 API | 称重传感器 driver 原为空壳 |
| `MDK-ARM/userDriver/driver_senser.c` | 新增实现 | 实现 HX711 DOUT/PD_SCK 时序、24bit 读取、重量换算、10ms 缓存更新 | 满足实时重量监测和投喂闭环 |

## 通信变量映射

- 状态上报使用 `remoteVar_TX[4]~[11]`，没有占用 APP 不可见的 `[0]~[3]`。
- 计划广播使用 `remoteVar_TX[12]~[15]`。
- APP 命令从 `remoteVar_RX` 读取；当前 `func_appcom` 将 RX 第 2 个 1B 字段映射到 `remoteInfo.systemEnable`，所以 `task_user1` 做了兼容读取。
- 任务层没有做字节序处理，仍由 `func_appcom` 负责。

## 已实现功能测试步骤

### 实时重量监测

1. CubeMX 将 HX711 `DOUT/DT` 配为输入，`PD_SCK/SCK` 配为推挽输出且默认低电平。
2. 空载启动，观察 `senserInfo.rawAdc` 是否随 10ms 周期更新。
3. 放置砝码后观察 `user1TaskInfo.currentWeightG` 变化。
4. 调用 `DRIVER_SENSER_SetCalibration()` 设置零点和比例系数后，重量应换算为克。

### 自动计划投喂

1. 发送计划设置命令：`planId=1`、`frequencySec=10`、`weightG=30`。
2. 保持 `planEnabled=1`，等待 10s。
3. 舵机执行开槽动作，`feedingState=1`。
4. 重量达到 `targetWeightG - 10g` 后，舵机执行关槽动作，`feedingState=0`。

### 手动投喂

1. 发送计划禁用命令。
2. 发送手动开启命令，舵机开槽，进入投喂中。
3. 发送手动关闭命令，舵机关槽并记录一次历史。

### 卡堵报警

1. 启动一次投喂。
2. 10s 内保持重量增长小于 1g。
3. 系统置位 `USER1_ALARM_MOTOR_JAM`，强制关槽，蜂鸣器短鸣。
4. 收到新的 APP 命令后清除卡堵报警。

### 余量报警与阈值

1. 发送阈值设置命令，例如 30g。
2. 当前重量大于 30g 时，`alarmFlags` bit0 为 0。
3. 当前重量小于 30g 时，`alarmFlags` bit0 为 1，蜂鸣器短鸣。

### 计划广播

1. 设置 1~5 个计划。
2. 每秒观察 `remoteVar_TX[12]~[15]`。
3. 5 秒内应轮播完整计划列表，空槽位 `planId=0`。

## 自检结果

- `func_appcom.c/h` 未修改。
- `task_system.c/h` 未修改。
- `userLib/` 未修改。
- `Core/` 未修改。
- 未整体清零 `remoteVar_TX`。
- 发送给 APP 的状态和计划数据均使用 `remoteVar_TX[4]~[15]`。
- task 层未直接调用 HAL。
- task 层未做字节序处理。
- 未使用动态内存。
- 已用 ARMCC 单独编译 `task_user1.c` 和 `driver_senser.c` 通过。

## 遗留接口说明

- 红外和 RFID 需求等待对应 driver 接入；当前 `petNearby`、`rfidPetId` 保持 0。
- Flash 断电保存等待扩展存储结构或新增安全的应用层分区 API；当前只做 RAM 数据管理和 dirty 标志。
- BLE 断电/连接报警当前采用命令心跳超时判断；若 ESP32 后续提供连接状态字段，可替换为私有同步字段读取。
