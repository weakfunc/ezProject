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
新建userFunc文件夹，在其中新建func_appcom模块，用于管理来自ESP32和发送给ESP32的数据流。我的最终目标是，把来自ESP32的数据通过appcom接口映射,保存到stm32的本地变量，同时，能够把stm32的本地变量通过APPCOM的接口映射发送给esp32. 关于发送的底层API已经由driver_ble模块实现

为实现上述功能，你需要在其中新建结构体数组，数组元素个数为8，每一个CMD字段管理一个结构体实例

    0x09~0x12：STM32向ESP32发送数据包

    0x13~0x16：EPS32向STM32发送数据包

并参考如下ESP32->APP的代码，完成STM32->ESP32的发送与接收代码。并封装为func_appcom模块API，集成在usertask。对于stm32发送，等号右侧是应该我手动配置的变量，对于stm32接收，等号左侧是应该我手动配置的变量。手动配置变量默认值给0
void appcom_task(void *arg)
{
    (void)arg;
    appcomInfo.active = true;

    while (1) {
        /* ================================================================
         * ESP32→APP 数据装填（手动逐字段赋值）
         * 格式：bleInfo.bleCmdFrameArr[CMD - 0x17].payload.varXXX = <来源>;
         * ================================================================ */

        /* ---- CMD 0x17 ← STM32 CMD 0x09 ---- */
        bleInfo.bleCmdFrameArr[0].payload.var_4b_1 = stm32Info.stm32CmdFrameArr[0].payload.var_4b_1;
        bleInfo.bleCmdFrameArr[0].payload.var_4b_2 = systemConfig.sys_time_s;
        bleInfo.bleCmdFrameArr[0].payload.var_1b_1 = stm32Info.stm32CmdFrameArr[0].payload.var_1b_1;
        bleInfo.bleCmdFrameArr[0].payload.var_1b_2 = stm32Info.stm32CmdFrameArr[0].payload.var_1b_2;

        /* ---- CMD 0x18 ← STM32 CMD 0x0A ---- */
        bleInfo.bleCmdFrameArr[1].payload.var_4b_1 = stm32Info.stm32CmdFrameArr[1].payload.var_4b_1;
        bleInfo.bleCmdFrameArr[1].payload.var_4b_2 = stm32Info.stm32CmdFrameArr[1].payload.var_4b_2;
        bleInfo.bleCmdFrameArr[1].payload.var_1b_1 = stm32Info.stm32CmdFrameArr[1].payload.var_1b_1;
        bleInfo.bleCmdFrameArr[1].payload.var_1b_2 = stm32Info.stm32CmdFrameArr[1].payload.var_1b_2;

        /* ---- CMD 0x19 ← STM32 CMD 0x0B ---- */
        bleInfo.bleCmdFrameArr[2].payload.var_4b_1 = stm32Info.stm32CmdFrameArr[2].payload.var_4b_1;
        bleInfo.bleCmdFrameArr[2].payload.var_4b_2 = stm32Info.stm32CmdFrameArr[2].payload.var_4b_2;
        bleInfo.bleCmdFrameArr[2].payload.var_1b_1 = stm32Info.stm32CmdFrameArr[2].payload.var_1b_1;
        bleInfo.bleCmdFrameArr[2].payload.var_1b_2 = stm32Info.stm32CmdFrameArr[2].payload.var_1b_2;

        /* ---- CMD 0x1A ← STM32 CMD 0x0C ---- */
        bleInfo.bleCmdFrameArr[3].payload.var_4b_1 = stm32Info.stm32CmdFrameArr[3].payload.var_4b_1;
        bleInfo.bleCmdFrameArr[3].payload.var_4b_2 = stm32Info.stm32CmdFrameArr[3].payload.var_4b_2;
        bleInfo.bleCmdFrameArr[3].payload.var_1b_1 = stm32Info.stm32CmdFrameArr[3].payload.var_1b_1;
        bleInfo.bleCmdFrameArr[3].payload.var_1b_2 = stm32Info.stm32CmdFrameArr[3].payload.var_1b_2;

        /* ---- 发送所有 ESP32→APP 帧 ---- */
        driver_ble_send_all();

        /* ================================================================
         * APP→ESP32→STM32 数据装填（手动逐字段赋值）
         * 格式：stm32Info.stm32CmdFrameArr[4 + (CMD-0x13)].payload.varXXX = <来源>;
         * ================================================================ */

        /* ---- CMD 0x13 ← APP CMD 0x21 ---- */
        stm32Info.stm32CmdFrameArr[4].payload.var_4b_1 = bleInfo.bleCmdFrameArr[4].payload.var_4b_1;
        stm32Info.stm32CmdFrameArr[4].payload.var_4b_2 = bleInfo.bleCmdFrameArr[4].payload.var_4b_2;
        stm32Info.stm32CmdFrameArr[4].payload.var_1b_1 = bleInfo.bleCmdFrameArr[4].payload.var_1b_1;
        stm32Info.stm32CmdFrameArr[4].payload.var_1b_2 = bleInfo.bleCmdFrameArr[4].payload.var_1b_2;

        /* ---- CMD 0x14 ← APP CMD 0x22 ---- */
        stm32Info.stm32CmdFrameArr[5].payload.var_4b_1 = bleInfo.bleCmdFrameArr[5].payload.var_4b_1;
        stm32Info.stm32CmdFrameArr[5].payload.var_4b_2 = bleInfo.bleCmdFrameArr[5].payload.var_4b_2;
        stm32Info.stm32CmdFrameArr[5].payload.var_1b_1 = bleInfo.bleCmdFrameArr[5].payload.var_1b_1;
        stm32Info.stm32CmdFrameArr[5].payload.var_1b_2 = bleInfo.bleCmdFrameArr[5].payload.var_1b_2;

        /* ---- CMD 0x15 ← APP CMD 0x23 ---- */
        stm32Info.stm32CmdFrameArr[6].payload.var_4b_1 = bleInfo.bleCmdFrameArr[6].payload.var_4b_1;
        stm32Info.stm32CmdFrameArr[6].payload.var_4b_2 = bleInfo.bleCmdFrameArr[6].payload.var_4b_2;
        stm32Info.stm32CmdFrameArr[6].payload.var_1b_1 = bleInfo.bleCmdFrameArr[6].payload.var_1b_1;
        stm32Info.stm32CmdFrameArr[6].payload.var_1b_2 = bleInfo.bleCmdFrameArr[6].payload.var_1b_2;

        /* ---- CMD 0x16 ← APP CMD 0x24 ---- */
        stm32Info.stm32CmdFrameArr[7].payload.var_4b_1 = bleInfo.bleCmdFrameArr[7].payload.var_4b_1;
        stm32Info.stm32CmdFrameArr[7].payload.var_4b_2 = bleInfo.bleCmdFrameArr[7].payload.var_4b_2;
        stm32Info.stm32CmdFrameArr[7].payload.var_1b_1 = bleInfo.bleCmdFrameArr[7].payload.var_1b_1;
        stm32Info.stm32CmdFrameArr[7].payload.var_1b_2 = bleInfo.bleCmdFrameArr[7].payload.var_1b_2;

        /* ---- 发送所有 ESP32→STM32 帧 ---- */
        driver_stm32_send_all();

        vTaskDelay(pdMS_TO_TICKS(appcomInfo.cycle_ms));
    }
}





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

