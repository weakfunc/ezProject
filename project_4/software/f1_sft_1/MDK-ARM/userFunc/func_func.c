#include "func_func.h"
#include "stdlib_pid.h"

/* func_func模块公有信息结构体 */
funcFuncInfo_t funcFuncInfo;

/*============================================================================
 * 水温闭环控制（PID）
 *============================================================================*/

/* PID增益及限幅参数（位置式，加热器单向驱动） */
#define WATER_TEMP_KP       (1000.0f)  /* 比例增益：1℃误差→1000占空比（5%功率） */
#define WATER_TEMP_KI       (800.0f)   /* 积分增益：消除稳态误差，配合iMax防饱和 */
#define WATER_TEMP_KD       (0.0f)     /* 微分增益：热系统响应慢，D项引入噪声置零 */
#define WATER_TEMP_P_MAX    (20000.0f) /* P项限幅：覆盖全量程 */
#define WATER_TEMP_I_MAX    (15000.0f) /* I项限幅：半量程，防积分饱和 */
#define WATER_TEMP_OUT_MAX  (20000.0f) /* 输出限幅：对应最大占空比 */

/* 初始化水温PID控制器。 */
void FUNC_FUNC_WaterTempCtrlInit(void){
  STDLIB_PID_Init(FUNC_FUNC_DEP_WATER_TEMP_PID_ID,
                  WATER_TEMP_KP, WATER_TEMP_KI, WATER_TEMP_KD,
                  WATER_TEMP_P_MAX, WATER_TEMP_I_MAX, WATER_TEMP_OUT_MAX);
  funcFuncInfo.waterTempCtrl.targetTemp = 0.0f;
}

/* 以senserInfo.ds18b20Temp为反馈，计算PID并同步设置两路加热模块占空比。
 * 加热器不具备制冷能力，PID负输出（水温超标）时占空比置零，依靠自然冷却。 */
void FUNC_FUNC_WaterTempCtrlUpdate(float targetTemp){
  float    output;
  uint32_t duty;

  funcFuncInfo.waterTempCtrl.targetTemp = targetTemp;

  output = STDLIB_PID_Calc(FUNC_FUNC_DEP_WATER_TEMP_PID_ID,
                            targetTemp, senserInfo.ds18b20Temp);

  /* 负输出置零：加热器只能加热，超温时停止输出，自然冷却 */
  duty = (output > 0.0f) ? (uint32_t)output : 0U;

  DRIVER_SENSER_HeatSetDuty(SENSER_HEAT_CH_1, duty);
  DRIVER_SENSER_HeatSetDuty(SENSER_HEAT_CH_2, duty);
}

/*============================================================================
 * 电机转速闭环控制（PID）
 *============================================================================*/

/* PID增益及限幅参数（位置式，双极性驱动） */
#define MOTOR_SPEED_KP       (5.0f)    /* 比例增益：100RPM误差→500PWM */
#define MOTOR_SPEED_KI       (1.0f)    /* 积分增益：消除稳态误差 */
#define MOTOR_SPEED_KD       (0.0f)    /* 微分增益：编码器噪声大，D项置零 */
#define MOTOR_SPEED_P_MAX    (1000.0f) /* P项限幅 */
#define MOTOR_SPEED_I_MAX    (500.0f)  /* I项限幅：防积分饱和 */
#define MOTOR_SPEED_OUT_MAX  (1000.0f) /* 输出限幅：对应PWM满量程 */

/* 初始化所有电机转速PID控制器，期望转速置零。 */
void FUNC_FUNC_MotorSpeedCtrlInit(void){
  uint8_t i;

  for(i = 0U; i < TB6612_MOTOR_COUNT; i++){
    STDLIB_PID_Init((uint8_t)(FUNC_FUNC_DEP_MOTOR_SPEED_PID_BASE_ID + i),
                    MOTOR_SPEED_KP, MOTOR_SPEED_KI, MOTOR_SPEED_KD,
                    MOTOR_SPEED_P_MAX, MOTOR_SPEED_I_MAX, MOTOR_SPEED_OUT_MAX);
    funcFuncInfo.motorSpeedCtrl[i].targetRpm = 0;
  }
}

/* 以EncoderGetRpm为反馈，对所有电机执行一次PID计算并调用MotorSetSpeed。
 * targetRpm：期望转速（RPM），正值正转，负值反转。
 * 须每10ms与EncoderUpdate同周期调用。 */
void FUNC_FUNC_MotorSpeedCtrlUpdate(int16_t targetRpm){
  uint8_t i;
  float   output;

  for(i = 0U; i < TB6612_MOTOR_COUNT; i++){
    funcFuncInfo.motorSpeedCtrl[i].targetRpm = targetRpm;
    output = STDLIB_PID_Calc((uint8_t)(FUNC_FUNC_DEP_MOTOR_SPEED_PID_BASE_ID + i),
                              (float)targetRpm,
                              (float)DRIVER_TB6612_EncoderGetRpm(i));
    DRIVER_TB6612_MotorSetSpeed(i, (int16_t)output);
  }
}
