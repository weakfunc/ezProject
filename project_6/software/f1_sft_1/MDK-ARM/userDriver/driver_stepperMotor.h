#ifndef __DRIVER_STEPPER_MOTOR_H__
#define __DRIVER_STEPPER_MOTOR_H__

#include "stdlib_usart.h"

/*============================================================================
 * 固件版本选择
 * 定义 STEPPER_FIRMWARE_X：使用 X 固件 API（SetTorque / SetPosDirect / SetPosTrapezoid）。
 * 注释掉此宏      ：使用 Emm 固件 API（SetPos，脉冲数接口）。
 * 每个工程只能选择一种固件，两套 API 互斥。
 *============================================================================*/

 // #define STEPPER_FIRMWARE_X

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

/* 电机 ID（串口地址，可按实际拨码修改）。 */
#define STEPPER_ADDR_PITCH     0x01U  /* 俯仰轴 */
#define STEPPER_ADDR_YAW       0x02U  /* 偏航轴 */

/* 通讯帧固定校验码。 */
#define STEPPER_CHECKSUM       0x6BU

/* 按电机 ID 访问反馈数据结构体（motorId = STEPPER_ADDR_*，地址从 1 起连续编号）。 */
#define STEPPER_INFO(motorId)  stepperMotorInfo[(motorId) - 1U]

#if !defined(STEPPER_FIRMWARE_X)
/* Emm固件脉冲数换算常数：3200脉冲 = 1圈（默认1.8°步进，细分16）。 */
#define STEPPER_PULSE_PER_REV  3200U
#endif

/* 运动模式（位置模式参数）。 */
#define STEPPER_MODE_REL_LAST  0U  /* 相对上一目标位置的相对运动 */
#define STEPPER_MODE_ABS       1U  /* 相对坐标零点的绝对位置运动 */
#define STEPPER_MODE_REL_CUR   2U  /* 相对当前实时位置的相对运动 */

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

/* 单轴步进电机反馈数据结构体。 */
typedef struct {
  uint8_t  addr;           /* 电机串口地址 */
  int32_t  realPos_01deg;  /* 实时位置，单位 0.1°，已含符号 */
  int16_t  realSpd_01rpm;  /* 实时转速，单位 0.1RPM，已含符号 */
  uint16_t encoder;        /* 线性化编码器值，0-65535 对应 0-360° */
  int32_t  posErr_001deg;  /* 位置误差，单位 0.01°，已含符号 */
  uint8_t  motorStatus;    /* 电机状态标志（参考 STEPPER_STATUS_* 宏）*/
  uint8_t  homeStatus;     /* 回零状态标志（参考 STEPPER_HOME_* 宏）*/
  uint8_t  lastAck;        /* 最近一次收到的应答码 */
} stepperMotorInfo_t;

/* 步进电机反馈数据数组（公有，建议通过 STEPPER_INFO(motorId) 宏访问）。 */
extern stepperMotorInfo_t stepperMotorInfo[STEPPER_MOTOR_CNT];

/*============================================================================
 * 两套固件通用 API
 * motorId：电机地址（STEPPER_ADDR_PITCH / STEPPER_ADDR_YAW）
 * 速度参数：正值 = 顺时针，负值 = 逆时针
 * 所有命令立即执行（无需同步触发）
 *============================================================================*/

/* 初始化步进电机驱动，配置电机地址并注册 USART2 自定义接收回调。 */
void DRIVER_STEPPER_Init(void);

/* 使能/失能电机（功能码 F3）。
 * motorId: 电机地址
 * enable : 0=失能（松轴），1=使能（锁轴）
 */
void DRIVER_STEPPER_Enable(uint8_t motorId, uint8_t enable);

/* 立即停止电机（功能码 FE）。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_Stop(uint8_t motorId);

/* 触发回零（功能码 9A）。
 * motorId : 电机地址
 * homeMode: 回零模式（0=单圈就近, 1=单圈方向, 2=无限位碰撞,
 *           3=限位回零, 4=绝对零点, 5=上次掉电位置）
 */
void DRIVER_STEPPER_GoHome(uint8_t motorId, uint8_t homeMode);

/* 将当前位置角度清零（功能码 0A）。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ZeroPos(uint8_t motorId);

/* 解除堵转/过热/过流保护（功能码 0E）。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ClearError(uint8_t motorId);

/* 发送读取实时位置请求（功能码 36），响应存入 STEPPER_INFO(motorId).realPos_01deg。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadPos(uint8_t motorId);

/* 发送读取实时转速请求（功能码 35），响应存入 STEPPER_INFO(motorId).realSpd_01rpm。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadSpeed(uint8_t motorId);

/* 发送读取线性化编码器值请求（功能码 31），响应存入 STEPPER_INFO(motorId).encoder。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadEncoder(uint8_t motorId);

/* 发送读取电机状态标志请求（功能码 3A），响应存入 STEPPER_INFO(motorId).motorStatus。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadStatus(uint8_t motorId);

/* 发送读取回零状态标志请求（功能码 3B），响应存入 STEPPER_INFO(motorId).homeStatus。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadHomeStatus(uint8_t motorId);

/* 发送读取位置误差请求（功能码 37），响应存入 STEPPER_INFO(motorId).posErr_001deg。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadPosError(uint8_t motorId);

/* 转动指定圈数（两套固件通用）。
 * motorId: 电机地址
 * revs   : 转动圈数（正整数，圈数方向由 speed 符号决定）
 * speed  : 转动速度（RPM），正值=顺时针，负值=逆时针
 *          Emm固件：有效范围 -3000~+3000 RPM
 *          X  固件：有效范围 -3000~+3000 RPM（内部换算为 0.1RPM 单位）
 */
void DRIVER_STEPPER_RotateRevs(uint8_t motorId, uint32_t revs, int16_t speed);

/*============================================================================
 * 固件专有 API
 *============================================================================*/

#if defined(STEPPER_FIRMWARE_X)

/* 力矩模式控制（功能码 F5，X固件）。
 * motorId: 电机地址
 * slope  : 电流斜率（mA/S），范围 0-65535
 * current: 目标电流（mA），正值=顺时针，负值=逆时针，范围 -5000~+5000
 */
void DRIVER_STEPPER_SetTorque(uint8_t motorId, uint16_t slope, int16_t current);

/* 速度模式控制（功能码 F6，X固件）。
 * motorId: 电机地址
 * accel  : 加速度（RPM/S），范围 0-65535
 * speed  : 目标速度（0.1RPM），正值=顺时针，负值=逆时针，范围 -30000~+30000
 */
void DRIVER_STEPPER_SetSpeed(uint8_t motorId, uint16_t accel, int16_t speed);

/* 直通限速位置模式控制（功能码 FB，X固件）。
 * motorId: 电机地址
 * speed  : 运行速度（0.1RPM），正值=顺时针，负值=逆时针，范围 -30000~+30000
 * pos    : 位置角度（0.1°），范围 0-4294967295
 * mode   : 运动模式（STEPPER_MODE_*）
 */
void DRIVER_STEPPER_SetPosDirect(uint8_t motorId, int16_t speed,
                                 uint32_t pos, uint8_t mode);

/* 梯形曲线加减速位置模式控制（功能码 FD，X固件）。
 * motorId : 电机地址
 * accAccel: 加速加速度（RPM/S），范围 0-65535
 * decAccel: 减速加速度（RPM/S），范围 0-65535
 * maxSpeed: 最大速度（0.1RPM），正值=顺时针，负值=逆时针，范围 -30000~+30000
 * pos     : 位置角度（0.1°），范围 0-4294967295
 * mode    : 运动模式（STEPPER_MODE_*）
 */
void DRIVER_STEPPER_SetPosTrapezoid(uint8_t motorId, uint16_t accAccel,
                                    uint16_t decAccel, int16_t maxSpeed,
                                    uint32_t pos, uint8_t mode);

#else  /* STEPPER_FIRMWARE_EMM */

/* 速度模式控制（功能码 F6，Emm固件）。
 * motorId: 电机地址
 * vel    : 目标转速（RPM），正值=顺时针，负值=逆时针，范围 -3000~+3000
 * acc    : 加速度档位，范围 0-255；0=直接以目标速度启动（无加减速）；
 *          档位越大加速越快，公式：每隔 (256-acc)*50us 改变 1RPM
 */
void DRIVER_STEPPER_SetSpeed(uint8_t motorId, int16_t vel, uint8_t acc);

/* 位置模式控制（功能码 FD，Emm固件）。
 * motorId: 电机地址
 * vel    : 运行转速（RPM），正值=顺时针，负值=逆时针，范围 -3000~+3000
 * acc    : 加速度档位，范围 0-255（同 SetSpeed 说明）
 * clk    : 脉冲数，3200脉冲=1圈（STEPPER_PULSE_PER_REV），范围 0-4294967295
 * raF    : 运动模式（STEPPER_MODE_REL_LAST / STEPPER_MODE_ABS / STEPPER_MODE_REL_CUR）
 */
void DRIVER_STEPPER_SetPos(uint8_t motorId, int16_t vel, uint8_t acc,
                           uint32_t clk, uint8_t raF);

#endif  /* STEPPER_FIRMWARE_X */

#endif  /* __DRIVER_STEPPER_MOTOR_H__ */
