## 项目背景

- 芯片: STM32F103C8T6，HAL库开发
- IDE: Keil
- RTOS: FreeRTOS CMSIS_V2
- 已配置外设:
    USART1，USART2，USART3，
    TIM2(CH1,CH2)，TIM3(CH1,CH2,CH3,CH4)，TIM4(CH1,CH4)，GPIO
- 分层架构: HAL库底层 -> stdlib层 -> driver层 -> func层 -> task层
- 已安装python的pypdf插件用于读取pdf文件

## 任务

实现云台闭环控制功能模块 `func_gimbal.c / func_gimbal.h`。

### 控制目标
以 driver_mpu6050 提供的陀螺仪角度作为反馈，通过位置式PID控制 driver_steer 两路舵机，
使云台实际角度跟随目标角度指令。

### 硬件信息
先阅读 driver_steer.h 和 driver_mpu6050.h，了解已有API后再实现，
如有疑问停下来提问，不要自行假设接口。

### 控制架构
目标角度(yaw_target, pitch_target)
↓
误差 e = 目标角度 - MPU6050反馈角度
↓
位置式PID（调用 stdlib_pid）
↓
补偿量 u(k)（单位：度）
↓
舵机指令 = 目标角度 + u(k)，限幅 [10°, 170°]
↓
driver_steer 设置舵机角度



### PID说明
- yaw和pitch各一个独立PID实例，调用 stdlib_pid 提供的位置式PID
- 初始参数 Kp=1.0, Ki=0.0, Kd=0.1，仅做结构占位，实际需调参

## 集成方式

- 在 task_user1.c 中以10ms为子周期调用 Gimbal_Task()
- 在任务初始化处调用 Gimbal_Init()
- 调用 Gimbal_SetTarget(90.0f, 90.0f) 设置初始目标为中位

