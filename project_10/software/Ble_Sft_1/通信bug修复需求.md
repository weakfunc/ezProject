# BLE 写入队列改造需求

## 一、问题背景

当前 APP 向 ESP32 写入 BLE characteristic 时，存在丢包问题。

**现象：**
- Logcat 中 `onCharacteristicWrite` 回调频繁出现 `status=17`
- APP 需要发送好几次按钮指令，ESP32 才能收到一次
- ESP32 端示波器显示 TX 引脚在丢包时无任何波形输出

**根因：**
- `status=17` 是 Android 的 `GATT_INSUF_RESOURCES`（协议栈资源不足）
- 当前代码在前一次写操作未收到 `onCharacteristicWrite` 回调时，就发起了下一次写入
- Android 蓝牙栈的发送队列未排空就收到新请求，直接拒绝并返回 status=17，数据包根本没发到空中

**已确认：**
- ESP32 端 characteristic 同时声明了 `WRITE` 和 `WRITE_NO_RSP` 能力
- 问题必须在 APP 端解决，无法靠 ESP32 修复

## 二、改造目标

实现一个 BLE 写入队列管理器，保证：
1. 任意时刻只有一个 BLE 写操作在进行中
2. 上层调用方可以无脑入队，不需要关心当前是否有写操作在进行
3. 前一次写完成（`onCharacteristicWrite` 回调）后，自动触发队列中下一帧的发送
4. 写失败（status != 0）时记录日志，但继续处理队列后续项（不阻塞队列）

## 三、开发前置步骤

**开发前必须执行：**

1. 浏览项目 BLE 相关源码目录，找到现有的 BLE 通信封装类（可能名为 `BleManager`、`BleConnectionManager`、`BleProtocol` 等）
2. 阅读以下内容并向我汇总：
   - 当前 `writeCharacteristic` 的调用位置（哪些类、哪些函数会触发 BLE 写）
   - 当前 `BluetoothGattCallback` 的 `onCharacteristicWrite` 实现
   - 当前的 writeType 设置（`WRITE_TYPE_DEFAULT` / `WRITE_TYPE_NO_RESPONSE`）
   - 当前帧构建逻辑在哪里（16 字节帧：包头 0x55 0xAA + ctrl + 10字节data + CRC8 + CNT + 包尾 0xFF）
3. 汇总后，**等我确认改造方案再动手**，不要直接修改

## 四、技术方案

### 4.1 新增 BleWriteQueue 类

新建文件，类名建议 `BleWriteQueue`，职责：
- 维护一个待发送帧的 FIFO 队列
- 维护一个 `pending` 标志，表示当前是否有写操作未完成
- 提供 `enqueue(data: ByteArray)` 接口给上层调用
- 提供 `onWriteCompleted(status: Int)` 接口给 GattCallback 调用

**关键约束：**
- `pending` 标志只能在 `onCharacteristicWrite` 回调中清零，**不能在调用 `writeCharacteristic()` 之后立即清零**
- 所有对队列和 `pending` 的操作必须加同步锁（`synchronized` 或 `Mutex`），保证线程安全
- 队列长度建议设上限（例如 32 帧），超过时丢弃最早的帧并打 warning 日志，避免内存堆积

### 4.2 集成到现有 BLE 模块

- 在原有 BLE 管理类中持有一个 `BleWriteQueue` 实例
- 把现有的所有 `gatt.writeCharacteristic()` 调用，改为调用 `bleWriteQueue.enqueue(data)`
- 在 `BluetoothGattCallback.onCharacteristicWrite()` 中调用 `bleWriteQueue.onWriteCompleted(status)`

### 4.3 writeType 保持 WRITE_TYPE_DEFAULT

- 不要改 writeType
- 带响应写配合队列，是最稳妥的组合

### 4.4 写失败处理

- `status == 0`：成功，继续发下一帧
- `status != 0`（包括 17）：打 warning 日志记录失败的 ctrl 字段和 CNT，**不重传，直接发下一帧**
- 应用层重传不在本次改造范围内

## 五、不要做的事

- **不要修改帧格式**（16 字节协议帧已固定）
- **不要修改 ESP32 端代码**（本次只改 APP）
- **不要修改 BLE 连接、扫描、订阅相关代码**（只改写入路径）
- **不要改 writeType 为 NO_RESPONSE**（即使能加快也不要改，会引入新的窗口溢出问题）
- **不要在写入路径上加 sleep / delay**（用回调驱动，不用定时器）
- **不要为了"提高吞吐"做并发写或批量写**（BLE 协议本身就是串行的）

## 六、验证清单

改造完成后，必须满足：
- [ ] 连续点击 50 次发送按钮，Logcat 中 `status=17` 不再出现
- [ ] 现有所有调用 BLE 写入的功能正常（不要漏改某处直接调 `writeCharacteristic` 的地方）
- [ ] 极端情况（队列堆积）下 APP 不崩溃、不 ANR
- [ ] 断连重连后队列状态正确（断连时清空队列、重置 pending 标志）

## 七、产出

1. 新建的 `BleWriteQueue` 类源文件
2. 修改后的 BLE 管理类（diff 形式说明改了哪几处）
3. 一段简短的自测说明：怎么验证改造生效