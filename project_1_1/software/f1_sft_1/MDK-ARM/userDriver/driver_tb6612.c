#include "driver_tb6612.h"

/*============================================================================
 * 内部配置（仅driver_tb6612模块内部使用）
 *============================================================================*/

/* 电机资源映射表 */
static const tb6612MotorMap_t tb6612MotorMap[TB6612_MOTOR_COUNT] = {
#if (TB6612_CFG_BOARD_HAS_INVERTER == 1U)
    [TB6612_MOTOR_A] = { TB6612_DEP_MOTOR_A_PWM_CH, TB6612_DEP_MOTOR_A_IO1_GPIO_ID, TB6612_DEP_GPIO_UNUSED },
    [TB6612_MOTOR_B] = { TB6612_DEP_MOTOR_B_PWM_CH, TB6612_DEP_MOTOR_B_IO1_GPIO_ID, TB6612_DEP_GPIO_UNUSED },
    [TB6612_MOTOR_C] = { TB6612_DEP_MOTOR_C_PWM_CH, TB6612_DEP_MOTOR_C_IO1_GPIO_ID, TB6612_DEP_GPIO_UNUSED },
    [TB6612_MOTOR_D] = { TB6612_DEP_MOTOR_D_PWM_CH, TB6612_DEP_MOTOR_D_IO1_GPIO_ID, TB6612_DEP_GPIO_UNUSED },
    [TB6612_MOTOR_E] = { TB6612_DEP_MOTOR_E_PWM_CH, TB6612_DEP_MOTOR_E_IO1_GPIO_ID, TB6612_DEP_GPIO_UNUSED },
#else
    [TB6612_MOTOR_A] = { TB6612_DEP_MOTOR_A_PWM_CH, TB6612_DEP_MOTOR_A_IO1_GPIO_ID, TB6612_DEP_MOTOR_A_IO2_GPIO_ID },
    [TB6612_MOTOR_B] = { TB6612_DEP_MOTOR_B_PWM_CH, TB6612_DEP_MOTOR_B_IO1_GPIO_ID, TB6612_DEP_MOTOR_B_IO2_GPIO_ID },
#endif
};

typedef struct{
	uint8_t motorID;
	int16_t duty;
}tb6612_t;
  
tb6612_t tb6612DebugInfo[TB6612_MOTOR_COUNT] = {
    {.motorID = 0, .duty = 100},
    {.motorID = 1, .duty = 100},
};

/* 默认状态初始化标志（默认速度为0） */
static uint8_t tb6612DefaultInited = 0U;

/* 计算速度绝对值 */
static uint16_t __DRIVER_TB6612_AbsSpeed(int16_t speed){
    if(speed < 0){
        return (uint16_t)(-speed);
    }
    return (uint16_t)speed;
}


/* 根据速度方向设置方向引脚 */
static void __DRIVER_TB6612_SetDirection(const tb6612MotorMap_t *motorMap, int16_t speed){
#if (TB6612_CFG_BOARD_HAS_INVERTER == 1U)
    if(speed > 0){
        STDLIB_COMMON_GpioWrite(motorMap->io1GpioId, TB6612_DEP_GPIO_LEVEL_HIGH);
    } else {
        STDLIB_COMMON_GpioWrite(motorMap->io1GpioId, TB6612_DEP_GPIO_LEVEL_LOW);
    }
#else
    if(speed > 0){
        STDLIB_COMMON_GpioWrite(motorMap->io1GpioId, TB6612_DEP_GPIO_LEVEL_HIGH);
        STDLIB_COMMON_GpioWrite(motorMap->io2GpioId, TB6612_DEP_GPIO_LEVEL_LOW);
    } else if(speed < 0){
        STDLIB_COMMON_GpioWrite(motorMap->io1GpioId, TB6612_DEP_GPIO_LEVEL_LOW);
        STDLIB_COMMON_GpioWrite(motorMap->io2GpioId, TB6612_DEP_GPIO_LEVEL_HIGH);
    } else {
        STDLIB_COMMON_GpioWrite(motorMap->io1GpioId, TB6612_DEP_GPIO_LEVEL_LOW);
        STDLIB_COMMON_GpioWrite(motorMap->io2GpioId, TB6612_DEP_GPIO_LEVEL_LOW);
    }
#endif
}

/* 首次调用时将所有电机置为默认0速 */
static void __DRIVER_TB6612_InitDefaultSpeed(void){
    for(uint8_t i = 0U; i < TB6612_MOTOR_COUNT; i++){
        __DRIVER_TB6612_SetDirection(&tb6612MotorMap[i], 0);
        STDLIB_TIM_PwmSetDuty(tb6612MotorMap[i].pwmCh, 0.0f);
    }
    tb6612DefaultInited = 1U;
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 电机转速控制：speed<0反转，speed>0正转，speed=0, speed最大值1000，停转 */
void DRIVER_TB6612_MotorSetSpeed(uint8_t motorId, int16_t speed){
    uint16_t absSpeed;

    if(tb6612DefaultInited == 0U){
        __DRIVER_TB6612_InitDefaultSpeed();
    }

    if(motorId >= TB6612_MOTOR_COUNT){
        return;
    }

    if(speed > TB6612_SPEED_MAX){
        speed = TB6612_SPEED_MAX;
    } else if(speed < TB6612_SPEED_MIN){
        speed = TB6612_SPEED_MIN;
    }

    __DRIVER_TB6612_SetDirection(&tb6612MotorMap[motorId], speed);

    absSpeed = __DRIVER_TB6612_AbsSpeed(speed);
    STDLIB_TIM_PwmSetPulse(tb6612MotorMap[motorId].pwmCh, absSpeed);
}

void DRIVER_TB6612_DebugMode(){
	for(int i=0; i<TB6612_MOTOR_COUNT; i++){
	  DRIVER_TB6612_MotorSetSpeed(tb6612DebugInfo[i].motorID, tb6612DebugInfo[i].duty);
	}
}
