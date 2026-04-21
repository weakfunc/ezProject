#ifndef __DRIVER_TB6612_H__
#define __DRIVER_TB6612_H__

#include <stdint.h>
#include "stdlib_common.h"
#include "stdlib_tim.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 *============================================================================*/
#define TB6612_DEP_GPIO_LEVEL_LOW             GPIO_LEVEL_LOW
#define TB6612_DEP_GPIO_LEVEL_HIGH            GPIO_LEVEL_HIGH

#define TB6612_DEP_MOTOR_A_PWM_CH             PWM_TIM3_CH1
#define TB6612_DEP_MOTOR_A_IO1_GPIO_ID        GPIO_ID_USER_IO_1
#define TB6612_DEP_MOTOR_A_IO2_GPIO_ID        GPIO_ID_USER_IO_2

#define TB6612_DEP_MOTOR_B_PWM_CH             PWM_TIM3_CH2
#define TB6612_DEP_MOTOR_B_IO1_GPIO_ID        GPIO_ID_USER_IO_3
#define TB6612_DEP_MOTOR_B_IO2_GPIO_ID        GPIO_ID_USER_IO_4

#define TB6612_DEP_MOTOR_C_PWM_CH             PWM_TIM3_CH3
#define TB6612_DEP_MOTOR_C_IO1_GPIO_ID        GPIO_ID_USER_IO_2

#define TB6612_DEP_MOTOR_D_PWM_CH             PWM_TIM3_CH4
#define TB6612_DEP_MOTOR_D_IO1_GPIO_ID        GPIO_ID_USER_IO_4

#define TB6612_DEP_MOTOR_E_PWM_CH             PWM_TIM4_CH1
#define TB6612_DEP_MOTOR_E_IO1_GPIO_ID        GPIO_ID_USER_IO_5

#define TB6612_DEP_GPIO_UNUSED                (0xFFU)

/* 编码器 FBD+/FBD- GPIO ID 映射（电机1=MOTOR_A，电机2=MOTOR_B） */
#define TB6612_DEP_ENCODER1_FBDP_GPIO_ID      GPIO_ID_USER_IO_5
#define TB6612_DEP_ENCODER1_FBDM_GPIO_ID      GPIO_ID_USER_IO_6
#define TB6612_DEP_ENCODER2_FBDP_GPIO_ID      GPIO_ID_USER_IO_7
#define TB6612_DEP_ENCODER2_FBDM_GPIO_ID      GPIO_ID_USER_IO_8

/* FBD+ HAL 引脚号，用于 HAL_GPIO_EXTI_Callback 中匹配中断触发源 */
#define TB6612_DEP_ENCODER1_FBDP_PIN          USER_IO_5_Pin
#define TB6612_DEP_ENCODER2_FBDP_PIN          USER_IO_7_Pin

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/

/* 用户配置宏：1=板载反相器，0=无板载反相器 */
#define TB6612_CFG_BOARD_HAS_INVERTER         (0U)

#define TB6612_MOTOR_A                        (0U)
#define TB6612_MOTOR_B                        (1U)
#define TB6612_MOTOR_C                        (2U)
#define TB6612_MOTOR_D                        (3U)
#define TB6612_MOTOR_E                        (4U)

#if (TB6612_CFG_BOARD_HAS_INVERTER == 1U)
#define TB6612_MOTOR_COUNT                    (5U)
#else
#define TB6612_MOTOR_COUNT                    (2U)
#endif

#define TB6612_SPEED_MIN                      (-1000)
#define TB6612_SPEED_MAX                      (300)

/* 编码器通道索引 */
#define TB6612_ENCODER_CH_1                   (0U)
#define TB6612_ENCODER_CH_2                   (1U)
#define TB6612_ENCODER_CH_COUNT               (2U)

/* 编码器每圈码槽数（光栅线数），用于 pulse → RPM 换算 */
#define TB6612_ENCODER_SLOTS_PER_REV          (11U)

/* 减速比（电机轴转速 / 输出轴转速），用于换算输出轴RPM */
#define TB6612_ENCODER_GEAR_RATIO             (19U)

/* EncoderUpdate调用周期（ms），须与task层调用间隔一致 */
#define TB6612_ENCODER_SAMPLE_MS              (10U)

/* 电机映射结构体 */
typedef struct {
  uint8_t pwmCh;
  uint8_t io1GpioId;
  uint8_t io2GpioId;
} tb6612MotorMap_t;

/* 编码器通道信息结构体 */
typedef struct {
  int16_t pulse; /* 每10ms窗口内的有符号脉冲数：正值为正转，负值为反转，0为静止 */
  int16_t rpm;   /* 转速（RPM）：正值为正转，负值为反转，由EncoderUpdate更新 */
} tb6612EncoderInfo_t;

/* TB6612模块信息结构体（每路电机一个元素） */
typedef struct {
  uint8_t              motorId; /* 电机ID */
  int16_t              duty;    /* 当前占空比（-1000~1000） */
  tb6612EncoderInfo_t  encoder; /* 编码器数据 */
} tb6612Info_t;

extern tb6612Info_t tb6612Info[TB6612_MOTOR_COUNT];

/* 电机转速控制：speed范围[-1000, +1000] */
void DRIVER_TB6612_MotorSetSpeed(uint8_t motorId, int16_t speed);

/* 读取EXTI累积脉冲，更新tb6612Info[].encoder.pulse及.rpm（含减速比换算），须每TB6612_ENCODER_SAMPLE_MS ms调用一次 */
void DRIVER_TB6612_EncoderUpdate(void);

/* 获取指定编码器通道每TB6612_ENCODER_SAMPLE_MS ms窗口内的有符号脉冲数，ch取值为TB6612_ENCODER_CH_x */
int16_t DRIVER_TB6612_EncoderGetPulse(uint8_t ch);

/* 获取指定编码器通道转速（RPM），正值为正转，负值为反转；由EncoderUpdate每TB6612_ENCODER_SAMPLE_MS ms更新 */
int16_t DRIVER_TB6612_EncoderGetRpm(uint8_t ch);

void DRIVER_TB6612_DebugMode(void);
#endif
