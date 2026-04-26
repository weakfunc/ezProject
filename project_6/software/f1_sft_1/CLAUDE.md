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
 - 分层架构: HAL库底层->stdlib层->driver层->func层(driver层驱动应用，如PID控制等)->task层
 - 已安装python的pypdf插件用于读取pdf文件

## 任务
  新建driver_stepperMotor模块，用于步进电机控制，步进电机自带驱动板，我们只需要串口发送控制帧即可完成控制。步进电机默认使用USART2。
  步进电机的手册我放在根目录ZDT_X42S第二代闭环步进电机用户手册V1.0.4_260401.pdf下
  当前步进电机是X版固件（手册中说明了固件分为X版和Emm版）
  我现在需要使用driver_stepperMotor模块实现一个二轴云台，根据手册实现相关必要的API，如力矩控制，编码器反馈，速度控制，位置控制等等
  每个API上方都需要写有清晰的注释。


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

- driver层只提供模块的基础API（读、写、初始化等），涉及模块应用逻辑（如PID闭环控制、状态机、策略）在func_func模块中实现

- FUNC层用于实现基于driver层模块提供API的复杂应用逻辑，并向TASK层提供API

- 不要修改task层任何.c的任务周期，userTask的基础周期是2ms，systemTask的基础周期是10ms。通过任务计数器取余的方式获得更长的子周期

- 如果对于工程硬件层面如接线，引脚分配不确定，必须先停下来确认，不要自行实现

  

## 集成方式

- 集成在userTASK，用于功能测试

## 验收标准

- 编译无 warning 无 error

