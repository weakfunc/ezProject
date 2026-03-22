# C3_Sft_1 BLE 工程说明手册

## 1. 工程概述

这是一个基于 **ESP-IDF 5.5.3 + NimBLE** 的 BLE 外设工程，实际硬件目标为 **ESP32-C3**。  
工程主体来自 Espressif 的 `bleprph` 示例，并在当前仓库中保留了适合继续开发的 BLE 外设框架。

当前工程已经具备的核心能力：

- BLE 广播、连接、断开后自动恢复广播
- 标准 GAP / GATT / ANS 服务初始化
- 自定义 128-bit GATT Service / Characteristic / Descriptor
- Characteristic 的读、写、通知、指示
- SMP 配对流程
- 串口输入 passkey / 数值比较确认
- 可选 Bonding、MITM、Secure Connections、扩展广播、随机地址、链路加密

工程名由顶层 `CMakeLists.txt` 定义为 `ble_Mcu_1`。

## 2. 目标芯片说明

本工程按 **ESP32-C3** 使用。

需要注意的是，仓库当前还残留了一些 `esp32s3` 的历史配置：

- `sdkconfig`
- `.vscode/settings.json`
- `dependencies.lock`

这不影响源码逻辑分析，因为这里使用的 ESP-IDF / NimBLE API 对 C3 和 S3 基本通用；但如果直接拿当前配置去编译、下载、调试，仍然建议先切回 C3 目标，避免工具链、OpenOCD 和缓存产物混用。

推荐命令：

```bash
idf.py set-target esp32c3
idf.py menuconfig
idf.py build
```

如果此前已经按 S3 编译过，建议额外执行一次：

```bash
idf.py fullclean
```

如果使用 VS Code 调试，也建议同步修改：

- `IDF_TARGET=esp32c3`
- `idf.openOcdConfigs = ["board/esp32c3-builtin.cfg"]`

## 3. 开发环境与依赖

### 3.1 软件环境

- ESP-IDF: `5.5.3`
- CMake: `3.16+`
- Python: 使用 ESP-IDF 工具链自带版本
- 构建系统: CMake + `idf.py`

### 3.2 组件依赖

工程通过 `main/idf_component.yml` 额外引入：

- `nimble_peripheral_utils`

该依赖不是从远程仓库下载，而是直接引用本机 `IDF_PATH` 下的示例目录：

```yaml
${IDF_PATH}/examples/bluetooth/nimble/common/nimble_peripheral_utils
```

这意味着：

- 本机必须有完整的 ESP-IDF 源码目录
- `IDF_PATH` 必须正确
- 如果换一套不完整的 IDF 环境，工程可能无法解析该组件

### 3.3 构建特性

顶层 `CMakeLists.txt` 开启了：

```cmake
idf_build_set_property(MINIMAL_BUILD ON)
```

表示工程尽量只编译最小必要组件，以缩短构建时间、减少不必要依赖。

## 4. 目录结构

| 路径 | 说明 |
| --- | --- |
| `CMakeLists.txt` | 工程顶层构建入口 |
| `sdkconfig` | 当前仓库保存的实际配置快照 |
| `sdkconfig.defaults` | 通用默认配置 |
| `main/main.c` | 应用入口、NimBLE 初始化、GAP 事件处理、广播启动 |
| `main/gatt_svr.c` | GATT 服务定义与访问回调 |
| `main/bleprph.h` | UUID 与 GATT 相关声明 |
| `main/Kconfig.projbuild` | `menuconfig` 下的示例配置项 |
| `main/idf_component.yml` | 额外组件依赖定义 |
| `tutorial/bleprph_walkthrough.md` | 上游示例的原理性讲解文档 |
| `build/` | 本地构建输出目录 |

## 5. 启动流程

程序主入口是 `app_main()`，整体流程如下：

1. 初始化 NVS，用于 PHY 校准数据以及 BLE 相关存储准备。
2. 调用 `nimble_port_init()` 初始化 NimBLE Host/Controller。
3. 设置 NimBLE Host 回调：
   - `reset_cb`
   - `sync_cb`
   - `gatts_register_cb`
   - `store_status_cb`
4. 根据 `menuconfig` 选项配置安全参数：
   - I/O Capability
   - Bonding
   - MITM
   - Secure Connections
   - Peer Address Resolve
5. 初始化 GATT Server。
6. 将设备名设置为 `nimble-bleprph`。
7. 初始化 BLE 存储配置。
8. 创建 NimBLE Host 任务。
9. 初始化串口 CLI，用于配对时输入 `key` 命令。
10. 在 `bleprph_on_sync()` 中推断本机地址类型并启动广播。

## 6. BLE 功能说明

### 6.1 广播行为

默认广播路径为 `bleprph_advertise()`，行为如下：

- 广播模式：General Discoverable
- 连接模式：Undirected Connectable
- 广播时长：`BLE_HS_FOREVER`
- 广播数据包含：
  - Flags
  - 发射功率
  - 设备名 `nimble-bleprph`
  - 16-bit 服务 UUID：`0x1811`（Alert Notification Service）

说明：

- 广播包里放的是 **ANS 标准服务 UUID**，不是自定义 128-bit 服务 UUID。
- 如果启用 `CONFIG_EXAMPLE_EXTENDED_ADV`，则会切到扩展广播路径 `ext_bleprph_advertise()`。

### 6.2 标准服务

`gatt_svr_init()` 中会初始化以下标准服务：

- GAP Service
- GATT Service
- Alert Notification Service（ANS）

其中 ANS 的 16-bit UUID `0x1811` 也会用于广播标识。

### 6.3 自定义 GATT 服务

除标准服务外，工程还注册了一个自定义 128-bit 主服务。

| 项目 | UUID | 权限 | 说明 |
| --- | --- | --- | --- |
| Primary Service | `59462f12-9543-9999-12c8-58b459a2712d` | - | 自定义服务 |
| Characteristic | `33333333-2222-2222-1111-111100000000` | Read / Write / Notify / Indicate | 1 字节数据，默认值 `0x00` |
| Descriptor | `34343434-2323-2323-1212-121201010101` | Read | 1 字节数据，默认值 `0x99` |

特征值的行为：

- 中心设备读取 Characteristic 时，返回 `gatt_svr_chr_val`
- 中心设备写入 Characteristic 时，会更新 `gatt_svr_chr_val`
- 写入完成后调用 `ble_gatts_chr_updated()`，向已订阅客户端触发 Notify / Indicate

如果启用 `CONFIG_EXAMPLE_ENCRYPTION`：

- Characteristic 的读写将附加加密权限
- Descriptor 的读取将附加加密权限

### 6.4 GAP 事件处理

`bleprph_gap_event()` 统一处理主要 BLE 事件：

- `BLE_GAP_EVENT_CONNECT`
- `BLE_GAP_EVENT_DISCONNECT`
- `BLE_GAP_EVENT_CONN_UPDATE`
- `BLE_GAP_EVENT_ADV_COMPLETE`
- `BLE_GAP_EVENT_ENC_CHANGE`
- `BLE_GAP_EVENT_NOTIFY_TX`
- `BLE_GAP_EVENT_SUBSCRIBE`
- `BLE_GAP_EVENT_MTU`
- `BLE_GAP_EVENT_REPEAT_PAIRING`
- `BLE_GAP_EVENT_PASSKEY_ACTION`
- `BLE_GAP_EVENT_AUTHORIZE`

默认策略是：

- 连接失败后重新广播
- 断开连接后重新广播
- 重复配对时删除旧 bond 并允许重新配对
- 未显式放行的 `AUTHORIZE` 请求默认拒绝

## 7. 配对与安全机制

### 7.1 menuconfig 可配置项

在 `Example Configuration` 菜单下可配置：

- I/O Capability
- Bonding
- MITM
- Secure Connections
- Extended Advertising
- Random Address
- Link Encryption
- Resolve Peer Address

### 7.2 串口交互

工程初始化了一个串口 CLI，用于在配对过程中输入密钥或确认结果。

可用输入格式：

- `key Y`
- `key N`
- `key 123456`

对应场景：

- 数值比较确认
- 输入对端要求的 passkey
- 接受或拒绝比较结果

### 7.3 代码中的 passkey 常量

当前代码里存在两个 passkey 相关常量：

- `123456`
  - 用于 `BLE_SM_IOACT_DISP` 场景
- `456789`
  - 仅在启用 `CONFIG_BT_NIMBLE_STATIC_PASSKEY` 时通过 `ble_sm_configure_static_passkey()` 设置

如果后续要做正式产品，建议统一这两个值，避免不同配对路径下行为不一致。

## 8. 当前仓库默认配置状态

以下内容基于仓库当前保存的 `sdkconfig` 读取，仅用于理解现状；如果你按 C3 重新 `set-target` 并保存配置，这里的实际值可能变化。

| 配置项 | 当前状态 |
| --- | --- |
| 目标芯片 | `esp32s3`（历史残留，应切回 C3） |
| 设备名 | 运行时被设置为 `nimble-bleprph` |
| I/O Capability | `Just Works` |
| Bonding | 关闭 |
| MITM | 关闭 |
| Secure Connections | 关闭 |
| Extended Advertising | 关闭 |
| Random Address | 关闭 |
| Link Encryption | 关闭 |
| Resolve Peer Address | 关闭 |
| Static Passkey | 关闭 |
| ATT Preferred MTU | `256` |
| Max Connections | `3` |
| Bond 信息持久化 | 关闭（`CONFIG_BT_NIMBLE_NVS_PERSIST` 未开启） |

## 9. 构建与烧录

### 9.1 推荐的 C3 构建步骤

```bash
idf.py set-target esp32c3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

说明：

- `COMx` 替换为实际串口号
- 退出串口监视器可使用 `Ctrl+]`

### 9.2 切换目标时的建议

如果之前按 S3 编译过，建议执行：

```bash
idf.py fullclean
idf.py set-target esp32c3
```

这样可以避免：

- 旧的 `sdkconfig` 残留
- `build/` 目录缓存污染
- 调试配置与芯片目标不匹配

## 10. 上手测试方法

可以使用 `nRF Connect`、`LightBlue` 或任意 BLE 调试 App 验证。

建议流程：

1. 烧录并打开串口监视器。
2. 观察日志中是否出现：
   - `BLE Host Task Started`
   - `Device Address: xx:xx:xx:xx:xx:xx`
3. 在手机或 PC 扫描设备 `nimble-bleprph`。
4. 连接设备。
5. 查找自定义 Service UUID：
   - `59462f12-9543-9999-12c8-58b459a2712d`
6. 读取 Characteristic：
   - `33333333-2222-2222-1111-111100000000`
7. 打开 Notify 或 Indicate。
8. 向该 Characteristic 写入 1 字节数据。
9. 检查是否收到通知，以及串口是否打印：
   - `Characteristic write`
   - `Notification/Indication scheduled for all subscribed peers`

如果启用了配对相关配置，还需要在串口中按提示输入：

- `key Y`
- `key N`
- `key 123456`

## 11. 已知注意事项

- 仓库目前名称、源码意图和配置快照并不完全一致：
  - 目录名指向 C3
  - 实际硬件是 C3
  - 但仓库里仍留有 S3 配置
- `main/idf_component.yml` 依赖本地 `IDF_PATH` 中的示例组件，不是完全自包含工程。
- `CONFIG_BT_NIMBLE_NVS_PERSIST` 当前未启用，Bond 信息通常不会在重启后可靠保留。
- 运行时设备名是 `nimble-bleprph`，会覆盖 `sdkconfig` 中 NimBLE GAP 默认名。
- 当前自定义 Characteristic 只存储 **1 字节** 数据，如果后续要承载实际业务协议，需要先扩展属性长度和读写逻辑。

## 12. 二次开发建议

如果这个工程后续要从示例升级为正式产品版本，优先建议处理下面几项：

1. 统一工程目标为 `esp32c3`，清理 S3 历史配置。
2. 将设备名、Service UUID、Characteristic UUID 替换为产品定义。
3. 明确业务协议格式；当前 Characteristic 只有 1 字节，不适合承载复杂数据。
4. 统一 passkey 策略，避免 `123456` 和 `456789` 并存。
5. 如需长期绑定，打开并验证 NVS 持久化相关配置。

