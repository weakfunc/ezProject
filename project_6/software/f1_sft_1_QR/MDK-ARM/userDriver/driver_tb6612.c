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

/* TB6612模块数据 */
tb6612Info_t tb6612Info[TB6612_MOTOR_COUNT] = {
  {.motorId = 0, .duty = 100, .encoder = {0}},
  {.motorId = 1, .duty = 100, .encoder = {0}},
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
 * 编码器读取（EXTI双边沿中断计数法）
 *============================================================================*/

/* 编码器通道资源映射表（私有） */
typedef struct {
  uint8_t fbdPlusGpioId;  /* FBD+信号 GPIO ID（双边沿EXTI触发） */
  uint8_t fbdMinusGpioId; /* FBD-信号 GPIO ID（读电平判方向） */
} tb6612EncoderMap_t;

static const tb6612EncoderMap_t encoderMap[TB6612_ENCODER_CH_COUNT] = {
  [TB6612_ENCODER_CH_1] = { TB6612_DEP_ENCODER1_FBDP_GPIO_ID, TB6612_DEP_ENCODER1_FBDM_GPIO_ID },
  [TB6612_ENCODER_CH_2] = { TB6612_DEP_ENCODER2_FBDP_GPIO_ID, TB6612_DEP_ENCODER2_FBDM_GPIO_ID },
};

/* 10ms窗口内的有符号脉冲累计值（ISR写入，EncoderUpdate读取并清零） */
static volatile int32_t encoderPulseAccumulator[TB6612_ENCODER_CH_COUNT];

/* FBD+双边沿EXTI回调：正交解码判方向，累加有符号脉冲数。
 * 双边沿中断时不能仅靠FBD-电平判方向（上升/下降沿FBD-状态相反会导致正反向抵消）。
 * 正确做法：同时读FBD+和FBD-当前电平，FBD+≠FBD-为正转，FBD+=FBD-为反转。
 * 覆盖HAL弱定义，由HAL_GPIO_EXTI_IRQHandler自动调用，无需额外注册。 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
  uint8_t fbdPlus;
  uint8_t fbdMinus;

  if(GPIO_Pin == TB6612_DEP_ENCODER1_FBDP_PIN){
    fbdPlus  = STDLIB_COMMON_GpioRead(encoderMap[TB6612_ENCODER_CH_1].fbdPlusGpioId);
    fbdMinus = STDLIB_COMMON_GpioRead(encoderMap[TB6612_ENCODER_CH_1].fbdMinusGpioId);
    /* 正交解码：FBD+≠FBD-为正转，FBD+=FBD-为反转 */
    if(fbdPlus != fbdMinus){
      encoderPulseAccumulator[TB6612_ENCODER_CH_1]++;
    } else {
      encoderPulseAccumulator[TB6612_ENCODER_CH_1]--;
    }
  } else if(GPIO_Pin == TB6612_DEP_ENCODER2_FBDP_PIN){
    fbdPlus  = STDLIB_COMMON_GpioRead(encoderMap[TB6612_ENCODER_CH_2].fbdPlusGpioId);
    fbdMinus = STDLIB_COMMON_GpioRead(encoderMap[TB6612_ENCODER_CH_2].fbdMinusGpioId);
    if(fbdPlus != fbdMinus){
      encoderPulseAccumulator[TB6612_ENCODER_CH_2]++;
    } else {
      encoderPulseAccumulator[TB6612_ENCODER_CH_2]--;
    }
  }
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

/* 将TB6612_ENCODER_SAMPLE_MS窗口内EXTI中断累积的有符号脉冲数转入tb6612Info[].encoder.pulse
 * 并清零累加器，同时换算RPM写入tb6612Info[].encoder.rpm。
 * 须每TB6612_ENCODER_SAMPLE_MS ms调用一次；使用临界区保证与ISR的原子操作。 */
void DRIVER_TB6612_EncoderUpdate(void){
  uint32_t primask;
  uint8_t  i;

  for(i = 0U; i < TB6612_ENCODER_CH_COUNT; i++){
    primask = STDLIB_COMMON_EnterCritical();
    tb6612Info[i].encoder.pulse = (int16_t)encoderPulseAccumulator[i];
    encoderPulseAccumulator[i]  = 0;
    STDLIB_COMMON_ExitCritical(primask);
    /* 换算输出轴RPM：pulse × 60000 / (SLOTS_PER_REV × 2 × SAMPLE_MS × GEAR_RATIO) */
    tb6612Info[i].encoder.rpm = (int16_t)((int32_t)tb6612Info[i].encoder.pulse * 60000
                                          / ((int32_t)TB6612_ENCODER_SLOTS_PER_REV * 2
                                             * (int32_t)TB6612_ENCODER_SAMPLE_MS
                                             * (int32_t)TB6612_ENCODER_GEAR_RATIO));
  }
}

/* 获取指定编码器通道每10ms窗口内的有符号脉冲数，ch取值为TB6612_ENCODER_CH_x。 */
int16_t DRIVER_TB6612_EncoderGetPulse(uint8_t ch){
  return tb6612Info[ch].encoder.pulse;
}

/* 获取指定编码器通道转速（RPM），正值为正转，负值为反转。
 * 返回值由EncoderUpdate每10ms更新一次。 */
int16_t DRIVER_TB6612_EncoderGetRpm(uint8_t ch){
  return tb6612Info[ch].encoder.rpm;
}

void DRIVER_TB6612_DebugMode(void){
  for(int i = 0; i < TB6612_MOTOR_COUNT; i++){
    DRIVER_TB6612_MotorSetSpeed(tb6612Info[i].motorId, tb6612Info[i].duty);
  }
}
