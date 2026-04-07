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

 - 完成driver_senser模块。driver_senser模块是一个杂项传感器模块，它里面包含很多简单传感器的API。
 - 1. 水加热模块（PWM控制）
 -    输出PWM控制PWM调速器即可，支持两路水加热模块，默认使用PWM_TIM2_CH1和PWM_TIM2_CH2，默认占空比500（已配置满占空比1000）
 -    实现API：水温控制（输入参数：模块x， 占空比）

 - 2. 水位检测模块（读取IO电平）
 -    读取IO电平判断是否有水，支持两路水位检测模块，默认使用USERIO_9,USERIO_10，输入模式，默认高电平。当IO被拉低时有水，IO为高电平时无水。注：需要在stdlib_common中新增USER_IO_9,USER_IO_10，cubemax我已配置这两个IO为输入模式。
 -    实现API：（输出参数：是否有水）水位判断（输入参数：模块x） 

 - 3. DS18B20水温传感器修复）
    在 DS18B20_Init() 中将传感器精度配置为9位（写配置寄存器，Resolution = 00），转换时间缩短至94ms
DS18B20_GetTemp() 内部等待改为 vTaskDelay(pdMS_TO_TICKS(100))
task中调用周期改为200ms
返回值有效性判断：若返回值小于**-10.0f**（排除负数异常）或等于**-999.0f**（设备异常），则不更新 senserInfo.ds18b20Temp，维持上一次的有效值



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

