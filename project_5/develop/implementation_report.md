# STM32 需求实现报告

## 1. driver 接口汇总

### 保留的 userDriver 文件

`driver_a7670c.c/h`、`driver_board.c/h`、`driver_ds3231rtc.c/h`、`driver_gps.c/h`、`driver_imu.c/h`、`driver_mpu6050.c/h`、`driver_oled.c/h`、`driver_senser.c/h`、`driver_XRVoice.c/h`

### 向上提供 API 摘要

| 模块 | 公有结构体 | 初始化 | 更新/读取 | 其他功能 |
|------|------------|--------|-----------|----------|
| 超声波测距 | `senserInfo` | `DRIVER_SENSER_Init()` | `DRIVER_SENSER_GetHCSR04Distance()` | 距离单位为 mm，task 层换算 cm |
| OLED | `oledInfo` | `DRIVER_OLED_Init()` | `DRIVER_OLED_Refresh()` | `DRIVER_OLED_Clear()`、`DRIVER_OLED_ShowString()` |
| XRVoice 语音 | `xrVoiceInfo` | `DRIVER_XRVOICE_Init()` | `DRIVER_XRVOICE_Poll()` | `DRIVER_XRVOICE_FindBySemanticId()`、`DRIVER_XRVOICE_SendCmd()` |
| GPS | `gpsInfo` | `DRIVER_GPS_Init()` | `DRIVER_GPS_GetInfo()` | UART3 中断解析 NMEA |
| A7670C 短信 | `a7670cInfo` | `DRIVER_A7670C_Init()` | 无周期 Update | `DRIVER_A7670C_SendSms()` |
| DS3231 RTC | `ds3231RTCInfo` | `DRIVER_DS3231RTC_Init()` | `DRIVER_DS3231RTC_Update()` | `DRIVER_DS3231RTC_SetTime()`、`DS3231RTC_BCD_TO_DEC()` |
| MPU6050 加速度 | `mpu6050Info` | `DRIVER_MPU6050_Init()` | `DRIVER_MPU6050_Update()` | `DRIVER_MPU6050_GetAccel()`、`DRIVER_MPU6050_GetAngle()` |
| 开发板 | `boardInfo` | `DRIVER_BOARD_Init()` | `DRIVER_BOARD_KeyInfoUpdate()` | KEY1 清除摔倒标志，KEY2 校时到最近整点 |
| IMU 串口模块 | `imuInfo` | `DRIVER_IMU_Init()` | UART3 中断解析 | 本需求使用 MPU6050，未启用该模块 |

## 2. 功能实现状态

| 序号 | 功能描述 | 状态 | 说明 |
|------|----------|------|------|
| 1 | 超声波测距（2~600cm，精度≤0.1cm） | ✅ | 10ms 周期调用 HC-SR04 驱动，mm 换算为 cm，写入 `user1TaskInfo.distance_cm` |
| 2 | OLED 综合显示（覆盖所有模块） | ✅ | 50ms 周期刷新距离、GPS、跌倒、短信、日期时间、Yaw/Pitch 角度 |
| 3 | 语音播报距离及行走提示 | ⚠️ | 已按 XRVoice 语义编号播报障碍、左转、右转、直行、摔倒和整点；当前语音 driver 不支持任意文本和动态距离数字拼播 |
| 4 | GPS 定位获取坐标 | ✅ | 500ms 周期读取 `DRIVER_GPS_GetInfo()` 快照，更新定位标志和经纬度 |
| 5 | A7670C 短信发送位置给家人 | ✅ | 使用 `ALERT_PHONE_NUMBER` 宏配置号码，摔倒后发送含坐标或未定位提示的中文短信 |
| 6 | 跌倒检测（加速度融合）+ 短信报警 | ✅ | 10ms 周期计算合加速度，当前阈值：`1.6g` 冲击、`50ms` 回落确认，`3.0g` 自动复位；KEY1 可清除摔倒标志，短信成功后置 `fallAlertSent` 防重复 |
| 7 | 整点报时 | ✅ | 500ms 周期检查 DS3231 时间，整点触发 XRVoice 语义 16~27；KEY2 可将 RTC 四舍五入到最近整点并立即播报 |

## 3. driver 层修改记录

| 文件 | 修改类型 | 修改内容 | 原因 |
|------|----------|----------|------|
| `driver_ds3231rtc.c` / `driver_ds3231rtc.h` | 新增 API | 增加 `DRIVER_DS3231RTC_SetTime()`，按 BCD 原始时间结构体写回 DS3231 时间寄存器 | KEY2 需要把 RTC 写到最近整点 |

## 4. 已实现功能测试步骤

- **测距与 OLED**：将手掌放在 HC-SR04 前约 30cm，OLED 第 1 行应显示接近 `D: 30.0cm`；移开或超时后显示 `D:---  cm`。
- **语音行走引导**：让测距值小于 `GUIDE_OBSTACLE_CM`，XRVoice 应依次播报前方有障碍物、左转/右转、直行；转动设备使 yaw 变化超过 20° 后进入直行提示。
- **跌倒检测**：手持快速震动设备后短暂停止，OLED 应显示 `Fall:!FALL!`，语音播报摔倒提示。
- **KEY1 清除摔倒**：OLED 显示 `Fall:!FALL!` 后按下 KEY1，OLED 应恢复 `Fall:OK`，并允许下次摔倒重新触发短信。
- **KEY2 整点校时**：将 DS3231 设置为 `10:20` 后按 KEY2，应写回 `10:00` 并播报十点整；设置为 `10:40` 后按 KEY2，应写回 `11:00` 并播报十一点整；`23:40` 会进位到次日 `00:00` 并播报十二点整。
- **短信报警**：A7670C 自检通过后触发跌倒，手机应收到 `ALERT_PHONE_NUMBER` 配置号码发出的中文短信；有 GPS 时包含经纬度，无 GPS 时提示未定位。
- **上电测试短信**：A7670C 初始化完成且任务循环运行 5s 后，手机应先收到 `STM32 test SMS OK` 测试短信，之后才处理摔倒报警短信。
- **GPS 显示**：在开阔环境等待 GPS 输出有效 NMEA，OLED 应显示 `GPS:OK`，并刷新 Lat/Lon。
- **整点报时**：将 DS3231 时间调到任意小时的 `xx:59:50`，过整点后应触发对应 1~12 点整语义播报；若当时语音忙或有更高优先级报警，则跳过本次报时。

## 5. 验证清单

| 序号 | 检查项 | 确认 |
|------|--------|------|
| 1 | `func_appcom.c` / `func_appcom.h` 未做任何修改 | ✅ |
| 2 | 本次 task 层代码未出现 `0x17~0x1A`、`0x21~0x24` 等 APP 端帧号 | ✅ |
| 3 | `remoteVar_TX` / `remoteVar_RX` 未被整体清零 | ✅ |
| 4 | `TX[0]~[3]` 未用于向 APP 发送数据 | ✅ |
| 5 | 未使用 `malloc`，全部静态分配 | ✅ |
| 6 | 新增/修改注释使用中文 | ✅ |
| 7 | 私有变量/函数定义在 `.c`，公有定义在 `.h` | ✅ |
| 8 | `task_system.c` 未被修改 | ✅ |
| 9 | `userLib/` 和 `Core/` 下文件未被修改 | ✅ |
| 10 | 跌倒检测逻辑已实现 | ✅ |
| 11 | 短信报警有防重复机制（`fallAlertSent`） | ✅ |
| 12 | 预设手机号以宏定义方式配置，未硬编码在逻辑中 | ✅ |

备注：全工程中既有 driver 文件包含 `0x19`、`0x1A`、`0x21`、`0x24` 等硬件寄存器、OLED 字模或短信 Ctrl+Z 常量，这些不是 APP 端帧号；本次 task 层实现未新增 APP 帧号。

## 6. 编译验证

当前机器未在 PATH 或常见安装路径中找到 `UV4`、`armcc`、`armclang`，因此未执行 Keil 实际编译。已完成静态检查：`task_user1.c/h` 中无 `malloc`、无 `remoteVar_TX/RX` 使用、无禁用 APP 帧号、无旧字段名残留。
