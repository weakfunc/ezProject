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
#define TB6612_SPEED_MAX                      (1000)

/* 电机映射结构体 */
typedef struct {
    uint8_t pwmCh;
    uint8_t io1GpioId;
    uint8_t io2GpioId;
} tb6612MotorMap_t;

/* 电机转速控制：speed范围[-1000, +1000] */
void DRIVER_TB6612_MotorSetSpeed(uint8_t motorId, int16_t speed);

void DRIVER_TB6612_DebugMode(void);
#endif
