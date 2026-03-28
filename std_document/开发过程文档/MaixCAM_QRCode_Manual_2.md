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
| `[3] ~ [12]` | 二维码内容 | UTF-8 编码，最多 **10 字节**，不足补 `0x00` |
| `[13]` | CRC8 | 对 `[0]~[12]` 共 13 字节的 CRC8 校验值；`ENABLE_CRC=False` 时固定为 `0x00` |
| `[14]` | CNT | 包计数，`0x00~0xFF` 循环递增 |
| `[15]` | `0xFF` | 包尾 |

控制字段说明：

0x00：STM32下位机向模块发送数据包

0x01~0x04：摄像头模块向STM32发送数据包

0x05~0x08:   串口屏模块向STM32发送数据包 

0x09~0x12：STM32向ESP32发送数据包

0x13~0x16：EPS32向STM32发送数据包

0x17~0x20：ESP32向APP发送数据包

0x21~0x24：APP向ESP32发送数据包

### 数据包示例

二维码内容为 `ABC`，CRC 开启，CNT=0 时：

```
55 AA 01 41 42 43 00 00 00 00 00 00 00 A3 00 FF
                                       ↑   ↑
                                      CRC CNT
```

CRC 关闭时：

```
55 AA 01 41 42 43 00 00 00 00 00 00 00 00 00 FF
                                       ↑
                                     0x00（禁用）
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
HEX: 55 AA 01 41 42 43 00 00 00 00 00 00 00 A3 00 FF
QR:  ABC
CRC: A3 (校验值)  CNT: 0
========================================
```

CRC 关闭时：

```
========================================
已发送数据包
HEX: 55 AA 01 41 42 43 00 00 00 00 00 00 00 00 00 FF
QR:  ABC
CRC: 00 (已禁用)  CNT: 1
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
| `DATA_MAX_LEN` | `10` | 数据段最大字节数，超出部分截断 |
| `ENABLE_CRC` | `True` | CRC8 校验开关：`True` 开启校验，`False` 关闭（CRC 字段填 `0x00`） |
| `device` | `/dev/ttyS0` | 串口设备路径 |

### CRC 开关使用方式

```python
ENABLE_CRC = True    # 开启：[13] = 对前13字节的CRC8计算值
ENABLE_CRC = False   # 关闭：[13] = 0x00，STM32端跳过校验即可
```

终端输出会同步提示当前 CRC 状态：

```
CRC: A3 (校验值)   CNT: 0    <- ENABLE_CRC = True
CRC: 00 (已禁用)   CNT: 0    <- ENABLE_CRC = False
```

---

## STM32 接收端解析建议

建议使用**中断逐字节接收 + 状态机解析**，按包头 `0x55 0xAA` 对齐，避免 DMA 错位问题：

```c
// CRC8 校验（多项式 0x07，与 MaixCAM 端一致）
uint8_t crc8(uint8_t *data, uint16_t len) {
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else            crc <<= 1;
        }
    }
    return crc;
}

// 状态机解析，逐字节喂入
typedef enum {
    WAIT_HEAD_1, WAIT_HEAD_2, WAIT_CTRL,
    WAIT_DATA, WAIT_CRC, WAIT_CNT, WAIT_TAIL
} ParseState;

ParseState parseState = WAIT_HEAD_1;
uint8_t    pktBuf[16];   // 完整数据包缓存
uint8_t    pktIdx  = 0;
uint8_t    dataIdx = 0;

void parseByte(uint8_t byte) {
    switch (parseState) {
    case WAIT_HEAD_1:
        if (byte == 0x55) { pktBuf[pktIdx++] = byte; parseState = WAIT_HEAD_2; }
        break;
    case WAIT_HEAD_2:
        if (byte == 0xAA) { pktBuf[pktIdx++] = byte; parseState = WAIT_CTRL; }
        else { pktIdx = 0; parseState = WAIT_HEAD_1; }
        break;
    case WAIT_CTRL:
        pktBuf[pktIdx++] = byte;   // 控制字段
        dataIdx = 0;
        parseState = WAIT_DATA;
        break;
    case WAIT_DATA:
        pktBuf[pktIdx++] = byte;
        if (++dataIdx >= 10) parseState = WAIT_CRC;   // 数据段 10 字节
        break;
    case WAIT_CRC:
        pktBuf[pktIdx++] = byte;   // CRC 字节
        parseState = WAIT_CNT;
        break;
    case WAIT_CNT:
        pktBuf[pktIdx++] = byte;   // CNT 字节
        parseState = WAIT_TAIL;
        break;
    case WAIT_TAIL:
        if (byte == 0xFF) {
            pktBuf[pktIdx++] = byte;
            // 数据包接收完整，验证 CRC
            uint8_t crc_calc = crc8(pktBuf, 13);      // 对前 13 字节校验
            uint8_t crc_recv = pktBuf[13];
            uint8_t cnt      = pktBuf[14];
            if (crc_recv == 0x00 || crc_calc == crc_recv) {
                // CRC 通过（或已禁用），处理数据
                processData(&pktBuf[3], cnt);          // 数据段从 [3] 开始，共 10 字节
            }
        }
        pktIdx = 0; parseState = WAIT_HEAD_1;
        break;
    }
}

// 处理二维码数据
void processData(uint8_t *data, uint8_t cnt) {
    printf("CNT:%d  QR:%s\r\n", cnt, (char*)data);
}

// 中断回调
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        parseByte(rxByte);
        HAL_UART_Receive_IT(&huart1, &rxByte, 1);
    }
}
```

> CRC 判断逻辑：`crc_recv == 0x00` 时视为 MaixCAM 端已禁用校验，直接放行；否则必须 `crc_calc == crc_recv` 才接受数据包。

---

## 注意事项

1. **二维码内容长度**：超过 10 字节的内容会被截断，建议二维码编码内容保持在 10 字节以内。
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
