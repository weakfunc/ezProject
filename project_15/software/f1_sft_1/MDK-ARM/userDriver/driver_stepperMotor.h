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

/* 电机 ID（串口地址，可按实际拨码修改）。 */
#define STEPPER_ADDR_PITCH     0x01U  /* 俯仰轴 */
#define STEPPER_ADDR_YAW       0x02U  /* 偏航轴 */

/* 通讯帧固定校验码。 */
#define STEPPER_CHECKSUM       0x6BU

/* 按电机 ID 访问反馈数据结构体（motorId = STEPPER_ADDR_*，地址从 1 起连续编号）。 */
#define STEPPER_INFO(motorId)  stepperMotorInfo[(motorId) - 1U]

/* Emm固件脉冲数换算常数：3200脉冲 = 1圈（默认1.8°步进，细分16）。 */
#define STEPPER_PULSE_PER_REV  3200U

/* 运动模式（位置模式参数）。 */
#define STEPPER_MODE_REL_LAST  0U  /* 相对上一目标位置的相对运动 */
#define STEPPER_MODE_ABS       1U  /* 相对坐标零点的绝对位置运动 */
#define STEPPER_MODE_REL_CUR   2U  /* 相对当前实时位置的相对运动 */

/* 回零模式（9A 指令参数）。 */
#define STEPPER_HOME_MODE_SINGLE_NEAREST  0U  /* 单圈就近回零 */
#define STEPPER_HOME_MODE_SINGLE_DIR      1U  /* 单圈方向回零 */
#define STEPPER_HOME_MODE_COLLISION       2U  /* 无限位碰撞回零 */
#define STEPPER_HOME_MODE_LIMIT           3U  /* 限位回零 */
#define STEPPER_HOME_MODE_ABS_ZERO        4U  /* 回到绝对位置坐标零点 */
#define STEPPER_HOME_MODE_PWRLOSS         5U  /* 回到上次掉电位置角度 */

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
  float    refPosRevs;     /* 参考位置已转圈数，单位 圈，已含符号 */
  float    refPosAngle_deg; /* 参考位置单圈角度，单位 °，范围 0-360 */
  uint8_t  refPosSign;     /* 参考位置符号，0=正，1=负 */
  uint32_t refPosRaw;      /* 参考位置原始值，65536 对应 360° */
  float    fbdPosRevs;     /* 反馈位置已转圈数，单位 圈，已含符号 */
  float    fbdPosAngle_deg; /* 反馈位置单圈角度，单位 °，范围 0-360 */
  uint8_t  fbdPosSign;     /* 反馈位置符号，0=正，1=负 */
  uint32_t fbdPosRaw;      /* 反馈位置原始值，65536 对应 360° */
  int16_t  fbdSpd_rpm;     /* 反馈转速，单位 RPM，已含符号 */
  uint16_t encoder;        /* 线性化编码器值，0-65535 对应 0-360° */
  float    homeZeroAngle_deg; /* 最近一次保存单圈回零零点时的电机角度，单位 ° */
  uint16_t homeZeroEncoder;   /* 最近一次保存单圈回零零点时的编码器值 */
  uint8_t  homeZeroValid;      /* STM32 侧零点是否有效 */
  float    relHomeZeroAngle_deg; /* 相对保存零点的角度，顺时针为正，逆时针为负，单位 ° */
  uint8_t  posErrSign;     /* 位置误差符号，0=正，1=负 */
  uint32_t posErrRaw;      /* 位置误差原始值，65536 对应 360° */
  float    posErrAngle_deg; /* 位置误差角度，单位 °，已含符号 */
  int32_t  posErr_001deg;  /* 位置误差，单位 0.01°，已含符号 */
  float    busCurrent;      /* 总线电压，单位 V */
  float    phaseCurrent_A;  /* 相电流，单位 A */
  int16_t  driverTemp;      /* 驱动温度，已含符号 */
  uint8_t  motorStatus;    /* 电机状态标志（参考 STEPPER_STATUS_* 宏）*/
  uint8_t  homeStatus;     /* 回零状态标志（参考 STEPPER_HOME_* 宏）*/
  uint8_t  lastAck;        /* 最近一次收到的应答码 */
} stepperMotorInfo_t;

/* 步进电机反馈数据数组（公有，建议通过 STEPPER_INFO(motorId) 宏访问）。 */
extern stepperMotorInfo_t stepperMotorInfo[STEPPER_MOTOR_CNT];

/*============================================================================
 * Emm 固件 API
 * motorId：电机地址（STEPPER_ADDR_PITCH / STEPPER_ADDR_YAW）
 * 速度参数：正值 = 顺时针，负值 = 逆时针
 * 控制命令可在 DRIVER_STEPPER_Update() 的弱钩子中直接发送。
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

/* 设置单圈回零的零点位置（功能码 93）。
 * motorId : 电机地址
 * saveFlag: 0=不存储, 1=存储到电机，掉电不丢失
 */
void DRIVER_STEPPER_SetHomeZero(uint8_t motorId, uint8_t saveFlag);

/* 请求保存全部电机当前单圈回零零点。
 * 实际指令会在 DRIVER_STEPPER_Update() 内逐条下发。
 */
void DRIVER_STEPPER_RequestSaveAllHomeZero(void);

/* 请求所有电机按板端使能状态切换。
 * enable=1: 使能全部电机并触发单圈就近回零；enable=0: 失能全部电机。
 */
void DRIVER_STEPPER_RequestBoardEnable(uint8_t enable);

/* 取出并清除零点 Flash 保存请求；返回 1 表示上层需要调用 STDLIB_FLASH_Save()。 */
uint8_t DRIVER_STEPPER_TakeFlashSaveRequest(void);

/* 将当前位置角度清零（功能码 0A）。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ZeroPos(uint8_t motorId);

/* 解除堵转/过热/过流保护（功能码 0E）。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ClearError(uint8_t motorId);

/* 发送读取实时位置请求（功能码 36），响应存入 STEPPER_INFO(motorId).fbdPosRevs / fbdPosAngle_deg。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadPos(uint8_t motorId);

/* 发送读取实时转速请求（功能码 35），响应存入 STEPPER_INFO(motorId).fbdSpd_rpm。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadSpeed(uint8_t motorId);

/* 发送读取线性化编码器值请求（功能码 31），响应存入 STEPPER_INFO(motorId).encoder。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadEncoder(uint8_t motorId);

/* 发送读取位置误差请求（功能码 37），响应存入 STEPPER_INFO(motorId).posErr_001deg。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadPosError(uint8_t motorId);

/* 发送读取总线电压请求（功能码 26），响应存入 STEPPER_INFO(motorId).busCurrent，单位 V。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadBusCurrent(uint8_t motorId);

/* 发送读取相电流请求（功能码 27），响应 mA 转换为 A 后存入 STEPPER_INFO(motorId).phaseCurrent_A。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadPhaseCurrent(uint8_t motorId);

/* 发送读取驱动温度请求（功能码 39），响应存入 STEPPER_INFO(motorId).driverTemp。
 * motorId: 电机地址
 */
void DRIVER_STEPPER_ReadDriverTemp(uint8_t motorId);

/* 参数查询/控制发送状态机（2ms 任务周期调用）：
 * 0=轴0全部参数，1=轴0控制钩子，2=轴1全部参数，
 * 3=轴1控制钩子，4=通用预留功能钩子。
 */
void DRIVER_STEPPER_Update(void);

/* 仅控制状态机（无反馈轮询，1ms 任务周期调用）：
 * 0=Axis0（pitch）控制钩子，1=Axis1（yaw）控制钩子，完整周期 2ms。
 * 适用于高频扫描，牺牲位置反馈换取最低命令延迟。
 */
void DRIVER_STEPPER_UpdateCtrlOnly(void);

/* 控制钩子。默认弱函数为空，上层可定义同名强函数覆盖，并在其中直接调用控制 API。 */
void DRIVER_STEPPER_Axis0CtrlHook(void);
void DRIVER_STEPPER_Axis1CtrlHook(void);
void DRIVER_STEPPER_CommonCtrlHook(void);

/* 转动指定圈数（相对当前位置）。
 * motorId: 电机地址）
 * vel    : 转动速度（RPM），正值=顺时针，负值=逆时针
 * acc    : 加速度档位，0-255；0=直接以目标速度启动，档位越大加速越快
 * revs   : 转动圈数（正整数，圈数方向由 vel 符号决定）
 */
void DRIVER_STEPPER_RotateRevs(uint8_t motorId, int16_t vel, uint8_t acc, uint32_t revs) ;

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
 * mode   : 运动模式（STEPPER_MODE_REL_LAST / STEPPER_MODE_ABS / STEPPER_MODE_REL_CUR）
 */
void DRIVER_STEPPER_SetPos(uint8_t motorId, int16_t vel, uint8_t acc,
                           uint32_t clk, uint8_t mode);

/* 读取系统状态参数（功能码 43，Emm固件）。
 * 响应写入 stepperMotorInfo_t：总线电压、相电流、编码器值、参考位置、
 * 实时转速、实时位置、位置误差、回零状态和电机状态。
 */
void DRIVER_STEPPER_ReadAllParams(uint8_t motorId);

#endif  /* __DRIVER_STEPPER_MOTOR_H__ */
