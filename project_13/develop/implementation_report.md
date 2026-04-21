# 两轮电动车防盗定位系统实现报告

## 一、driver清单

| driver | 公有结构体/API | 本次使用方式 |
|--------|----------------|--------------|
| `driver_board` | `boardInfo`、`DRIVER_BOARD_Init()`、`DRIVER_BOARD_KeyIsPressed()`、`DRIVER_BOARD_RgbSet()`、`DRIVER_BOARD_BuzzUpdate()` | 按键唤醒、RGB灯报警、蜂鸣器报警 |
| `driver_gps` | `gpsInfo`、`DRIVER_GPS_Init()`、`DRIVER_GPS_GetInfo()`、`DRIVER_GPS_GetPosition()` | 获取十进制度经纬度、定位有效状态和`gpsInfo.status` |
| `driver_mpu6050` | `mpu6050Info`、`DRIVER_MPU6050_Init()`、`DRIVER_MPU6050_Update()`、`DRIVER_MPU6050_GetAccel()` | 通过I2C_BUS_2周期读取三轴加速度，做震动检测 |
| `driver_oled` | `oledInfo`、`DRIVER_OLED_Init()`、`DRIVER_OLED_ShowString()`、`DRIVER_OLED_ShowFloat12x16()`、`DRIVER_OLED_DisplayOn/Off()` | 显示GPS状态、坐标、报警原因和休眠倒计时 |
| `driver_ble` | `bleInfo`、`DRIVER_BLE_Init()`、`DRIVER_BLE_SendFrame()` | 由`func_appcom`维护BLE帧收发，task层不直接发帧 |

## 二、功能实现状态

| 序号 | 功能描述 | 状态 | 说明 |
|------|----------|------|------|
| 1 | GPS数据读取与上报（1s周期） | ✅ | TX[4]/TX[5]/TX[6]/TX[8]按需求写入，帧序号每秒递增 |
| 2 | 位移监测防盗（>10m报警） | ✅ | 防盗开启后记录有效GPS参考点，超过10m置位报警来源2 |
| 3 | 震动检测防盗（加速度融合） | ✅ | MPU6050加速度合向量偏离1g超过阈值并连续确认后报警 |
| 4 | 声光报警（蜂鸣器+LED） | ✅ | 报警时100ms翻转蜂鸣器和红灯，报警清除后停止 |
| 5 | APP远程防盗开关控制 | ✅ | 当前框架将APP RX[2]映射为`remoteInfo.systemEnable`，task层按此实际接口读取 |
| 6 | APP远程报警消除 | ✅ | 读取RX[3]低8位，收到1后清除报警，不回写RX变量 |
| 7 | APP TX状态数据上报 | ✅ | TX[7]/TX[10]/TX[11]每100ms更新，GPS类数据每1s更新 |
| 8 | 休眠倒计时（30s）与KEY唤醒 | ✅ | 无报警时30s倒计时，休眠时关闭OLED并调用`STDLIB_SLEEP_EnterSleep()`，任意KEY唤醒 |
| 9 | OLED显示（GPS状态/坐标/报警原因/倒计时） | ✅ | OLED显示`gpsInfo.status`完整状态；底行显示`A:NONE/VIBRATION/OFFSET`和休眠倒计时 |

## 三、driver层修改记录

| 文件 | 修改类型 | 修改内容 | 原因 |
|------|----------|----------|------|
| `MDK-ARM/userDriver/driver_mpu6050.h` | 配置修改 | `MPU6050_DEP_I2C_BUS`由`I2C_BUS_1`改为`I2C_BUS_2` | MPU6050硬件连接到PA12/PA11对应的软件I2C_2 |
| `MDK-ARM/userDriver/driver_mpu6050.c` | 注释修改 | README说明更新为I2C_BUS_2 | 与实际总线配置保持一致 |
| `MDK-ARM/userLib/stdlib_common.h/.c` | 底层映射新增 | 新增`GPIO_ID_I2C_SDA_2`、`GPIO_ID_I2C_SCL_2`并映射到PA12/PA11 | 为软件I2C_2提供GPIO访问能力 |
| `MDK-ARM/userLib/stdlib_i2c.h` | 底层映射修改 | `I2C_BUS_2`映射到`GPIO_ID_I2C_SDA_2/GPIO_ID_I2C_SCL_2` | 使`STDLIB_I2C_*` API可访问I2C_2 |

## 四、测试步骤

- **GPS上报测试**：上电后将设备置于室外，APP或串口观察TX[4]、TX[5]是否为经纬度，TX[6]定位后为1，TX[8]每秒递增。
- **震动报警测试**：APP开启防盗后轻拍设备，连续触发约300ms后TX[7]应为1，蜂鸣器和LED开始闪烁报警。
- **位移报警测试**：APP开启防盗并等待GPS有效，移动设备超过10m，TX[7]应为1，报警来源内部状态为2。
- **APP控制测试**：APP下发防盗关闭后，TX[10]应为0，报警和参考GPS点清除；再次开启后TX[10]应为1。
- **报警消除测试**：报警中APP下发RX[3]=1，TX[7]应恢复0，蜂鸣器和LED停止报警。
- **休眠测试**：无报警且不按键等待30s，OLED关闭，TX[11]为1；按任意KEY后OLED恢复，倒计时回到30s。
- **OLED测试**：观察OLED第1行GPS状态应随`gpsInfo.status`显示为`GPS: DISCONNECTED`、`GPS: SEARCHING`或`GPS: WORKING`；触发震动报警时底行显示`A:VIBRATION`，触发位置偏移报警时底行显示`A:OFFSET`，无报警时显示`A:NONE`，底行右侧保留休眠倒计时秒数。

## 五、自查结果

- 本次未修改`func_appcom.c/.h`、`task_system.c/.h`和`Core`。
- 未整体清零`remoteInfo.remoteVar_TX`，只写入需求指定索引。
- 发给APP的数据仅写入TX[4]、TX[5]、TX[6]、TX[7]、TX[8]、TX[10]、TX[11]。
- RX读取未做额外字节序翻转，TX写入直接赋值。
- 未使用动态内存分配，全部为静态或栈上变量。
- 已修改的MPU6050 driver文件包含`[MODIFIED]`标注。
