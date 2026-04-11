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

请在 userLib/ 目录下新建 stdlib_flash.c 和 stdlib_flash.h，实现片内 Flash 掉电存储模块。
MCU 约束： STM32F103C8T6，HAL 库，Flash 页大小 1KB，写入单位半字（2字节），使用末页地址 0x0800FC00（若程序超 63KB 则往前移一页）。
设计模式： 模仿 appcom 的 remoteVar 风格——对外暴露一个固定的公有结构体 flashStore，所有需要持久化的模块直接对该结构体的字段赋值，最后统一调 STDLIB_FLASH_Save() 写入 Flash。Flash 模块本身不关心结构体内容，只负责整体存取。
stdlib_flash.h 公有部分：
c/* 持久化数据布局 — 按项目需求在此添加字段，总大小不超过 1016 字节 */
typedef struct {
    /* 示例字段，Claude Code 实现时保留此结构体定义，字段内容由用户填写 */
    uint32_t placeholder;  /* 占位，实际使用时替换 */
} flashStore_t;

extern flashStore_t flashStore;  /* 公有存储区，所有模块直接读写 */

void    STDLIB_FLASH_Init(void);     /* 从 Flash 加载数据到 flashStore；魔术字不匹配则写入默认值（全零）*/
void    STDLIB_FLASH_Save(void);     /* 将当前 flashStore 写入 Flash（擦页 + 半字写入）*/
void    STDLIB_FLASH_Reset(void);    /* flashStore 全零后写入 Flash */
uint8_t STDLIB_FLASH_IsValid(void);  /* 返回 1=上次 Init 读到有效数据，0=使用了默认值 */
stdlib_flash.c 私有实现要求：

Flash 页内布局：前 4 字节魔术字 0xA55A1234，后 4 字节存 sizeof(flashStore_t)（用于版本检测），之后紧跟 flashStore_t 数据体
Init 时：读魔术字和 size 均匹配才认为有效，加载数据；否则 flashStore 全零并调一次 Save
Save 时：先 HAL_FLASHEx_Erase() 擦整页，再用 HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, ...) 逐半字写入；若 sizeof 为奇数字节则补一字节再写
读取用 memcpy 直接从 Flash 地址拷贝
私有变量和函数写在 .c，公有接口写在 .h
注释中文，禁止 malloc


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

- 需把已实现API集成在task_user1中，用于测试API功能

## 验收标准

- 编译无 warning 无 error

