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
 - 分层架构: HAL库底层->stdlib层->driver层->func层(未实现)->task层
 - 已安装python的pypdf插件用于读取pdf文件

## 任务

- 根目录中defaultTask和motorTask是另一个工程的应用功能，它用二维码识别模块识别包裹。在这个工程里用K210识别包裹。
- 在这个工程里K210识别k210RxData到0x01，0x02，0x03分别对应目的地A,B,C
- 把defaultTask和motorTask中实现的功能移植到当前工程里来。对于QRcodePack_t里本工程没有的参数不用关心，只需要能够识别目的地然后控制舵机即可


## 约束
- 不要修改 CubeMX 生成的代码（USER CODE 区域以外的部分）
- 所有用户代码写在 USER CODE BEGIN/END 标记之间
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
- 对于stdlib层的.h文件,明确分层"内部配置"和"API接口"两类
- 对于不清晰的任务需求及时向我提问

## 集成方式

- 需集成在userApp中

## 验收标准

- 编译无 warning 无 error

