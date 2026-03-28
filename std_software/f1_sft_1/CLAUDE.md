## 项目背景

- 芯片: STM32F103C8T6，HAL库开发 
- IDE: Keil
- RTOS: FreeRTOS CMSIS_V2 
- 已配置外设: 
    USART1，
    USART2, 
    USART3, 
    TIM2(CH1,CH2), 
    TIM3(CH1,CH2,CH3,CH4)，
    TIM4(CH1,CH4)，
    GPIO,
 - 分层架构: HAL库底层->stdlib层->driver层->func层(暂未实现)->task层
 - 已安装python的pypdf插件用于读取pdf文件

## 任务

- 修改stdlib_usart模块，把模块内置的标准协议改为如下标准协议，删去原标准协议中的可变长度模式：
 固定 **16 字节**帧结构：

| 字节位置     | 值         | 说明                                                         |
| ------------ | ---------- | ------------------------------------------------------------ |
| `[0]`        | `0x55`     | 包头 1                                                       |
| `[1]`        | `0xAA`     | 包头 2                                                       |
| `[2]`        | `0x01`     | 控制字段                                                     |
| `[3] ~ [12]` | 二维码内容 | UTF-8 编码，最多 **10 字节**，不足补 `0x00`                  |
| `[13]`       | CRC8       | 对 `[0]~[12]` 共 13 字节的 CRC8 校验值；`ENABLE_CRC=False` 时固定为 `0x00` |
| `[14]`       | CNT        | 包计数，`0x00~0xFF` 循环递增                                 |
| `[15]`       | `0xFF`     | 包尾                                                         |

控制字段说明：

0x00：STM32下位机向模块发送数据包

0x01~0x04：摄像头模块向STM32发送数据包

0x05~0x08:   串口屏模块向STM32发送数据包 

0x09~0x12：STM32向ESP32发送数据包

0x13~0x16：EPS32向STM32发送数据包

0x17~0x20：ESP32向APP发送数据包

0x21~0x24：APP向ESP32发送数据包

通过数据格式也可以看到，每帧数据最多包含2个4字节变量，2个1字节变量。并且在stdlib_usart模块中留出最终解析数据的接口：CMD，4byteVar_1，4byteVar_2，1byteVar_1，1byteVar_2

typedef union {
    uint8_t raw[STM32_DATA_LEN];
    struct __attribute__((packed)) {
        uint32_t var_4b_1;
        uint32_t var_4b_2;
        uint8_t  var_1b_1;
        uint8_t  var_1b_2;
    };
} standardFrame_t;

typedef struct {
    /* --- 发送状态 --- */
    uint8_t            tx_cnt;       /* 发送帧计数器，0x00~0xFF 循环 */

    /* --- 接收状态 --- */
    uint8_t            cmd;                    /* 最近一帧的控制字段（ctrl byte[2]） */
    standardFrame_t standardRxFrame;           /* 10 字节数据段，支持具名字段访问 */
    standardFrame_t standardTxFrame;           /* 10 字节数据段，支持具名字段访问 */
    bool               frame_ready;            /* 新帧就绪标志；读取后应清零 */
} usartInfo_t;



## 约束
- 不要修改 CubeMX 生成的代码（USER CODE 区域以外的部分）
- 不使用 malloc，全部静态分配
- 代码注释用中文
- 代码正文首行缩进两个空格
- 参考UserCode文件夹下已有代码的规范和命名规则
- 为每个函数和必要的变量添加注释,注释格式参考USERCODE文件夹下其它函数的注释
- 私有结构体定义,宏定义均写在.c文件中，对外的结构体定义,宏定义写在.h文件中
- 不要跨层调用函数（func层调用stdlib层除外）,若低层级提供的API无法实现功能,先完善低层级API提供接口后,再在上层调用.跨层之间实现解耦
- func层可调用stdlib层api
- 不要额外实现我在"任务"章节没有要求的功能
- 对于driver层的.h文件,明确分成“向下依赖”和“向上提供”两类，并且注释标明向下依赖了哪些stdlib
- 仅在driver层减少不必要的边界检查
- 对于driver层的模块，都要新建一个且仅一个以该模块名称命名（模块名+Info_t，如：ws2812Info_t ws2812Info）的公有结构体，并在h中extern，用于管理该模块的相关数据。对于一个模块中有多个通道控制的情况，使用结构体数组管理该模块
- driver层只会向下调用stdlib层，driver层之间不会相互调用
- 对于stdlib层的.h文件,明确分层"内部配置"和"API接口"两类
- 遇到缺失文件时，优先清理引用，不要新增空 h/c 文件
- 如果对于工程硬件层面如接线，引脚分配不确定，必须先停下来确认，不要自行实现

## 集成方式

- 无需集成，仅提供API接口

## 验收标准

- 编译无 warning 无 error

