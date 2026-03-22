# MaixCAM 二维码识别与串口发送手册

## 概述

本程序运行于 **MaixCAM** 平台，实现以下功能：

- 实时摄像头画面采集与显示
- 二维码自动识别与边框标注
- 识别结果通过串口以固定格式数据包发送至 STM32
- 基于状态机的发送控制逻辑，防止重复发送

---

## 硬件连接

| MaixCAM | STM32 |
|---------|-------|
| TX (`/dev/ttyS0`) | RX |
| RX | TX |
| GND | GND |

> 波特率：**115200**，双方必须一致。

---

## 数据包格式

固定 **16 字节**帧结构：

| 字节位置 | 值 | 说明 |
|---------|-----|------|
| `[0]` | `0x55` | 包头 1 |
| `[1]` | `0xAA` | 包头 2 |
| `[2]` | `0x01` | 控制字段 |
| `[3] ~ [14]` | 二维码内容 | UTF-8 编码，最多 12 字节，不足补 `0x00` |
| `[15]` | `0xFF` | 包尾 |

### 数据包示例

二维码内容为 `ABC` 时，发送的 16 字节为：

```
55 AA 01 41 42 43 00 00 00 00 00 00 00 00 00 FF
```

---

## 状态机逻辑

程序采用三态状态机控制发送行为，确保每个二维码只发送一次数据包。

```
          二维码出现
 IDLE ──────────────→ SENT（立即发包一次）
  ↑                      │
  │                      │ 二维码消失
  │                      ↓
  └───── 冷却结束 ←── COOLDOWN
         (≥ COOLDOWN_MS)
```

### 各状态说明

| 状态 | 屏幕显示 | 说明 |
|------|---------|------|
| `IDLE` | `State: IDLE`（白色） | 等待二维码出现，允许发送 |
| `SENT-WAIT` | `State: SENT-WAIT`（绿色） | 已发送，等待二维码离开画面 |
| `COOLDOWN` | `State: COOLDOWN`（黄色） | 冷却计时中，倒计时结束后回到 IDLE |

### 完整流程示例

```
1. 二维码 A 出现 → 发送一帧数据包 → 进入 SENT-WAIT
2. 二维码 A 持续在画面中 → 不发送任何数据
3. 二维码 A 消失 → 进入 COOLDOWN，开始倒计时
4. 倒计时结束（默认 2s）→ 回到 IDLE
5. 二维码 B 出现 → 发送一帧数据包 → 重复以上流程
```

---

## 屏幕显示说明

| 行位置 | 内容 | 颜色 |
|--------|------|------|
| 第 0 行（y=0） | `Scan: <当前检测到的内容>` 或 `Scan: None` | 蓝色 / 白色 |
| 第 1 行（y=20） | `State: IDLE / SENT-WAIT / COOLDOWN` | 白 / 绿 / 黄 |
| 第 2 行（y=40） | `Sent: <最后一次发送的内容>` 或 `Sent: --` | 绿色 / 白色 |
| 第 3 行（y=60） | `CD: x.xs`（COOLDOWN 倒计时）或 `CD: --` | 黄色 / 白色 |

---

## 终端输出说明

每次成功发送数据包时，终端打印：

```
========================================
已发送数据包
HEX: 55 AA 01 41 42 43 00 00 00 00 00 00 00 00 00 FF
QR:  ABC
========================================
```

冷却结束时打印：

```
COOLDOWN结束 -> IDLE
```

---

## 可配置参数

| 参数名 | 默认值 | 说明 |
|--------|--------|------|
| `COOLDOWN_MS` | `2000` | 冷却时间（毫秒），发送后及二维码消失后的等待时间 |
| `DATA_MAX_LEN` | `12` | 数据段最大字节数，超出部分截断 |
| `device` | `/dev/ttyS0` | 串口设备路径 |

---

## STM32 接收端解析建议

建议使用**中断逐字节接收 + 状态机解析**，按包头 `0x55 0xAA` 对齐，避免 DMA 错位问题：

```c
// 状态机解析，逐字节喂入
void parseByte(uint8_t byte) {
    switch (state) {
    case WAIT_HEAD_1:
        if (byte == 0x55) state = WAIT_HEAD_2;
        break;
    case WAIT_HEAD_2:
        if (byte == 0xAA) state = WAIT_CTRL;
        else if (byte == 0x55) state = WAIT_HEAD_2;
        else state = WAIT_HEAD_1;
        break;
    case WAIT_CTRL:
        ctrlByte = byte;
        dataIndex = 0;
        state = WAIT_DATA;
        break;
    case WAIT_DATA:
        dataBuf[dataIndex++] = byte;
        if (dataIndex >= 12) state = WAIT_TAIL;
        break;
    case WAIT_TAIL:
        if (byte == 0xFF) {
            // 数据包接收完整，dataBuf 即为二维码内容
            processData(dataBuf);
        }
        state = WAIT_HEAD_1;
        break;
    }
}

// 中断回调
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        parseByte(rxByte);
        HAL_UART_Receive_IT(&huart1, &rxByte, 1);
    }
}
```

---

## 注意事项

1. **二维码内容长度**：超过 12 字节的内容会被截断，建议二维码编码内容保持在 12 字节以内。
2. **串口设备路径**：根据实际接线选择 `/dev/ttyS0` 或 `/dev/ttyS1`，可用 `uart.list_devices()` 查询可用设备。
3. **共地**：MaixCAM 与 STM32 必须共地，否则串口通信不稳定。
4. **电平**：MaixCAM IO 电平为 3.3V，与 STM32F103 直接兼容，无需电平转换。

---

## 部署：安装应用并设置开机自启动

### 第一步：在 MaixVision 中安装应用

1. 打开 MaixVision，连接 MaixCAM
2. 点击左下角 **"安装应用"** 按钮
3. 填写应用信息：

| 字段 | 填写内容 |
|------|---------|
| 应用 ID | `version01`（自定义，不能重复） |
| 应用名称 | `QRscan` |
| 版本号 | `1.0.0` |
| 开发者 | 自填 |

4. 点击 **"安装应用"** 完成安装
5. 安装完成后 MaixCAM 主菜单中会出现该应用

---

### 第二步：SSH 连接 MaixCAM

MaixCAM 的 IP 地址显示在 MaixVision 左下角，例如 `10.30.24.1`：

```bash
ssh root@10.30.24.1
# 默认密码：root
```

---

### 第三步：确认应用已安装

```bash
ls /maixapp/apps/
```

输出列表中能看到 `version01` 说明安装成功。

---

### 第四步：设置开机自启动

```bash
echo "version01" > /maixapp/auto_start.txt
```

验证写入是否成功：

```bash
cat /maixapp/auto_start.txt
# 输出：version01
```

---

### 第五步：重启验证

```bash
reboot
```

重启后 MaixCAM 应直接自动运行 QRscan 程序。

> 如需退出自启动应用返回主菜单，按设备上的 **user 键** 即可。

---

### 取消自启动

```bash
rm /maixapp/auto_start.txt
```
