#ifndef __DRIVER_STEPPER_MOTOR_H__
#define __DRIVER_STEPPER_MOTOR_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 * 依赖：stdlib_usart
 *============================================================================*/
/* 步进电机使用的串口端口（USART2）。 */
#define STEPPER_DEP_UART_PORT                UART_PORT2
/* 直接发送原始字节流（不使用内置标准协议封装）。 */
#define STEPPER_DEP_UART_SEND_RAW(buf, len)  STDLIB_USART_SendRaw(STEPPER_DEP_UART_PORT, (buf), (len))
/* 注册自定义字节接收回调。 */
#define STEPPER_DEP_UART_SET_CB(cb)          STDLIB_USART_SetCustomCb(STEPPER_DEP_UART_PORT, (cb))

/*============================================================================
 * 向上提供宏（driver层向func/task层提供）
 *============================================================================*/

/* 受控电机轴数量。 */
#define STEPPER_MOTOR_CNT      2U

/* 轴索引。 */
#define STEPPER_AXIS_PITCH     0U  /* 俯仰轴 */
#define STEPPER_AXIS_YAW       1U  /* 偏航轴 */

/* 电机默认串口地址（可按实际拨码修改）。 */
#define STEPPER_ADDR_PITCH     0x01U
#define STEPPER_ADDR_YAW       0x02U

/* 通讯帧默认校验码（固定值 0x6B）。 */
#define STEPPER_CHECKSUM       0x6BU

/* Emm固件脉冲数换算常数：3200脉冲 = 1圈（默认1.8°步进，细分16）。 */
#define STEPPER_PULSE_PER_REV  3200U

/* 运动模式（位置模式 raF 参数）。 */
#define STEPPER_MODE_REL_LAST  0U  /* 相对上一目标位置的相对运动 */
#define STEPPER_MODE_ABS       1U  /* 相对坐标零点的绝对位置运动 */
#define STEPPER_MODE_REL_CUR   2U  /* 相对当前实时位置的相对运动 */

/* 旋转方向。 */
#define STEPPER_DIR_CW         0U  /* 顺时针 */
#define STEPPER_DIR_CCW        1U  /* 逆时针 */

/* 同步标志。 */
#define STEPPER_SYNC_NOW       0U  /* 立即执行 */
#define STEPPER_SYNC_WAIT      1U  /* 缓存命令，等待同步触发 */

/* 电机应答码。 */
#define STEPPER_ACK_OK         0x02U  /* 命令正确接收 */
#define STEPPER_ACK_DONE       0x9FU  /* 动作执行完成（到位/回零完成）*/
#define STEPPER_ACK_ERR_PARAM  0xE2U  /* 参数错误或保护触发 */
#define STEPPER_ACK_ERR_FMT    0xEEU  /* 帧格式错误 */

/* 电机状态标志位（motorStatus 字段）。 */
#define STEPPER_STATUS_ENABLED 0x01U  /* 已使能 */
#define STEPPER_STATUS_POS_OK  0x02U  /* 已到达目标位置 */
#define STEPPER_STATUS_JAM     0x04U  /* 堵转标志 */
#define STEPPER_STATUS_JAM_P   0x08U  /* 堵转保护触发 */
#define STEPPER_STATUS_LIM_L   0x10U  /* 左限位开关触发 */
#define STEPPER_STATUS_LIM_R   0x20U  /* 右限位开关触发 */
#define STEPPER_STATUS_PWRLOSS 0x80U  /* 掉电标志 */

/* 回零状态标志位（homeStatus 字段）。 */
#define STEPPER_HOME_ENC_RDY   0x01U  /* 编码器就绪 */
#define STEPPER_HOME_CAL_RDY   0x02U  /* 校准表就绪 */
#define STEPPER_HOME_IN_PROG   0x04U  /* 正在回零 */
#define STEPPER_HOME_FAILED    0x08U  /* 回零失败 */
#define STEPPER_HOME_OTP       0x10U  /* 过热保护 */
#define STEPPER_HOME_OCP       0x20U  /* 过流保护 */

/* 单轴步进电机数据结构体。 */
typedef struct {
  uint8_t  addr;            /* 电机串口地址 */
  int32_t  realPos_01deg;  /* 实时位置，单位 0.1°，已含符号 */
  int16_t  realSpd_01rpm;  /* 实时转速，单位 0.1RPM，已含符号 */
  uint16_t encoder;        /* 线性化编码器值，0-65535 对应 0-360° */
  int32_t  posErr_001deg;  /* 位置误差，单位 0.01°，已含符号 */
  uint8_t  motorStatus;    /* 电机状态标志（参考 STEPPER_STATUS_* 宏）*/
  uint8_t  homeStatus;     /* 回零状态标志（参考 STEPPER_HOME_* 宏）*/
  uint8_t  lastAck;        /* 最近一次收到的应答码 */
} stepperMotorInfo_t;

/* 步进电机模块信息（公有，供上层直接访问）。 */
extern stepperMotorInfo_t stepperMotorInfo[STEPPER_MOTOR_CNT];

/* 初始化步进电机驱动，配置电机地址并注册 USART2 自定义接收回调。 */
void DRIVER_STEPPER_Init(void);

/* 使能/失能电机（功能码 F3）。
 * axis  : 轴索引（STEPPER_AXIS_PITCH 或 STEPPER_AXIS_YAW）
 * enable: 0=失能（松轴），1=使能（锁轴）
 * sync  : 同步标志（STEPPER_SYNC_NOW / STEPPER_SYNC_WAIT）
 */
void DRIVER_STEPPER_Enable(uint8_t axis, uint8_t enable, uint8_t sync);

/* 速度模式控制（功能码 F6，Emm固件）。
 * axis: 轴索引
 * dir : 方向（STEPPER_DIR_CW / STEPPER_DIR_CCW）
 * vel : 目标转速，单位 RPM，范围 0-3000
 * acc : 加速度档位，范围 0-255；0=直接以目标速度启动（无加减速）；
 *       档位越大加速越快，公式：每隔 (256-acc)*50us 改变 1RPM
 * sync: 同步标志（STEPPER_SYNC_NOW / STEPPER_SYNC_WAIT）
 */
void DRIVER_STEPPER_SetSpeed(uint8_t axis, uint8_t dir, uint16_t vel,
                             uint8_t acc, uint8_t sync);

/* 位置模式控制（功能码 FD，Emm固件）。
 * axis: 轴索引
 * dir : 方向（STEPPER_DIR_CW / STEPPER_DIR_CCW）
 * vel : 运行转速，单位 RPM，范围 0-3000
 * acc : 加速度档位，范围 0-255（同 SetSpeed 说明）
 * clk : 脉冲数，3200脉冲=1圈（默认1.8°步进，细分16），范围 0-4294967295
 * raF : 运动模式（STEPPER_MODE_REL_LAST / STEPPER_MODE_ABS / STEPPER_MODE_REL_CUR）
 * sync: 同步标志（STEPPER_SYNC_NOW / STEPPER_SYNC_WAIT）
 */
void DRIVER_STEPPER_SetPos(uint8_t axis, uint8_t dir, uint16_t vel,
                           uint8_t acc, uint32_t clk, uint8_t raF,
                           uint8_t sync);

/* 立即停止电机（功能码 FE）。
 * axis: 轴索引
 * sync: 同步标志
 */
void DRIVER_STEPPER_Stop(uint8_t axis, uint8_t sync);

/* 触发回零（功能码 9A）。
 * axis    : 轴索引
 * homeMode: 回零模式（0=单圈就近, 1=单圈方向, 2=无限位碰撞,
 *           3=限位回零, 4=绝对零点, 5=上次掉电位置）
 * sync    : 同步标志
 */
void DRIVER_STEPPER_GoHome(uint8_t axis, uint8_t homeMode, uint8_t sync);

/* 将当前位置角度清零（功能码 0A）。
 * axis: 轴索引
 */
void DRIVER_STEPPER_ZeroPos(uint8_t axis);

/* 解除堵转/过热/过流保护（功能码 0E）。
 * axis: 轴索引
 */
void DRIVER_STEPPER_ClearError(uint8_t axis);

/* 发送读取实时位置请求（功能码 36），响应存入 stepperMotorInfo[axis].realPos_01deg。
 * axis: 轴索引
 */
void DRIVER_STEPPER_ReadPos(uint8_t axis);

/* 发送读取实时转速请求（功能码 35），响应存入 stepperMotorInfo[axis].realSpd_01rpm。
 * axis: 轴索引
 */
void DRIVER_STEPPER_ReadSpeed(uint8_t axis);

/* 发送读取线性化编码器值请求（功能码 31），响应存入 stepperMotorInfo[axis].encoder。
 * axis: 轴索引
 */
void DRIVER_STEPPER_ReadEncoder(uint8_t axis);

/* 发送读取电机状态标志请求（功能码 3A），响应存入 stepperMotorInfo[axis].motorStatus。
 * axis: 轴索引
 */
void DRIVER_STEPPER_ReadStatus(uint8_t axis);

/* 发送读取回零状态标志请求（功能码 3B），响应存入 stepperMotorInfo[axis].homeStatus。
 * axis: 轴索引
 */
void DRIVER_STEPPER_ReadHomeStatus(uint8_t axis);

/* 发送读取位置误差请求（功能码 37），响应存入 stepperMotorInfo[axis].posErr_001deg。
 * axis: 轴索引
 */
void DRIVER_STEPPER_ReadPosError(uint8_t axis);

#endif
