#include "driver_senser.h"
#include "FreeRTOS.h"
#include "task.h"

/* 传感器模块公有信息结构体 */
senserInfo_t senserInfo;

/*============================================================================
 * 水加热模块
 *============================================================================*/

/* 水加热通道 PWM 通道映射表（私有） */
static const uint8_t heatPwmChMap[SENSER_HEAT_CH_COUNT] = {
    [SENSER_HEAT_CH_1] = SENSER_DEP_HEAT_CH1_PWM_CH,
    [SENSER_HEAT_CH_2] = SENSER_DEP_HEAT_CH2_PWM_CH,
};

/* 传感器驱动初始化：将两路加热模块占空比设为默认值，ds18b20Temp初始化为错误值。 */
void DRIVER_SENSER_Init(void){
  uint8_t i;

  for(i = 0U; i < SENSER_HEAT_CH_COUNT; i++){
    STDLIB_TIM_PwmSetPulse(heatPwmChMap[i], SENSER_HEAT_DUTY_DEFAULT);
    senserInfo.heat[i].duty = SENSER_HEAT_DUTY_DEFAULT;
  }
  senserInfo.ds18b20Temp = SENSER_DS18B20_TEMP_ERROR;
}

/* 设置指定水加热模块占空比。 */
void DRIVER_SENSER_HeatSetDuty(uint8_t ch, uint32_t duty){
  STDLIB_TIM_PwmSetPulse(heatPwmChMap[ch], duty);
  senserInfo.heat[ch].duty = duty;
}

/*============================================================================
 * 水位检测模块
 *============================================================================*/

/* 水位检测通道 GPIO ID 映射表（私有） */
static const uint8_t waterLvlGpioIdMap[SENSER_WATER_LVL_CH_COUNT] = {
    [SENSER_WATER_LVL_CH_1] = SENSER_DEP_WATER_LVL_CH1_GPIO_ID,
    [SENSER_WATER_LVL_CH_2] = SENSER_DEP_WATER_LVL_CH2_GPIO_ID,
};

/* 更新水位检测状态，读取 IO 电平并写入 senserInfo。 */
void DRIVER_SENSER_WaterLvlUpdate(void){
  uint8_t level;
  uint8_t i;

  for(i = 0U; i < SENSER_WATER_LVL_CH_COUNT; i++){
    level = STDLIB_COMMON_GpioRead(waterLvlGpioIdMap[i]);
    senserInfo.waterLvl[i].hasWater = (level == SENSER_DEP_WATER_LVL_HAS_WATER_LEVEL) ? 1U : 0U;
  }
}

/* 获取指定水位检测通道是否有水，返回 1=有水，0=无水。 */
uint8_t DRIVER_SENSER_WaterLvlGet(uint8_t ch){
  return senserInfo.waterLvl[ch].hasWater;
}

/*============================================================================
 * DS18B20 水温传感器（单总线1-Wire协议）
 *============================================================================*/

/* DS18B20 1-Wire命令定义 */
#define DS18B20_CMD_SKIP_ROM     (0xCCU)  /* 跳过ROM匹配，单设备时使用 */
#define DS18B20_CMD_CONVERT_T    (0x44U)  /* 启动温度转换 */
#define DS18B20_CMD_READ_SCRPAD  (0xBEU)  /* 读取暂存器 */
#define DS18B20_CMD_WRITE_SCRPAD (0x4EU)  /* 写暂存器（TH/TL/Config） */

/* DS18B20 9位精度配置字节：R1=0,R0=0,低5位固定为1 → 0x1F，转换时间约94ms */
#define DS18B20_CFG_9BIT         (0x1FU)

/* DS18B20 返回值有效范围下限，负数视为无效不更新上次结果（水温不应为负） */
#define DS18B20_TEMP_VALID_MIN   (0.0f)

/* DS18B20 DQ引脚 GPIO ID */
#define DS18B20_DQ_GPIO_ID       SENSER_DEP_DS18B20_GPIO_ID

/* 发出复位脉冲并检测存在脉冲，返回1=设备存在，0=无设备。 */
static uint8_t __DRIVER_SENSER_DS18B20Reset(void){
  uint32_t primask;
  uint8_t  presence;

  /* 拉低DQ 480µs（复位脉冲） */
  primask = STDLIB_COMMON_EnterCritical();
  STDLIB_COMMON_GpioModeOutput(DS18B20_DQ_GPIO_ID, GPIO_LEVEL_LOW);
  STDLIB_COMMON_ExitCritical(primask);
  STDLIB_DWT_DelayUs(480U);

  /* 释放总线，等待70µs后采样存在脉冲 */
  primask = STDLIB_COMMON_EnterCritical();
  STDLIB_COMMON_GpioModeInput(DS18B20_DQ_GPIO_ID);
  STDLIB_DWT_DelayUs(70U);
  presence = STDLIB_COMMON_GpioRead(DS18B20_DQ_GPIO_ID);
  STDLIB_COMMON_ExitCritical(primask);

  /* 等待剩余复位时隙结束 */
  STDLIB_DWT_DelayUs(410U);

  /* 低电平表示设备存在 */
  return (presence == GPIO_LEVEL_LOW) ? 1U : 0U;
}

/* 向1-Wire总线写入1位数据。 */
static void __DRIVER_SENSER_DS18B20WriteBit(uint8_t bit){
  uint32_t primask;

  if(bit != 0U){
    /* 写1：拉低≤15µs后释放，关中断保证时序精度 */
    primask = STDLIB_COMMON_EnterCritical();
    STDLIB_COMMON_GpioModeOutput(DS18B20_DQ_GPIO_ID, GPIO_LEVEL_LOW);
    STDLIB_DWT_DelayUs(5U);
    STDLIB_COMMON_GpioModeInput(DS18B20_DQ_GPIO_ID);
    STDLIB_COMMON_ExitCritical(primask);
    STDLIB_DWT_DelayUs(55U);
  } else {
    /* 写0：拉低60~120µs，时序宽松无需关中断 */
    STDLIB_COMMON_GpioModeOutput(DS18B20_DQ_GPIO_ID, GPIO_LEVEL_LOW);
    STDLIB_DWT_DelayUs(65U);
    STDLIB_COMMON_GpioModeInput(DS18B20_DQ_GPIO_ID);
    STDLIB_DWT_DelayUs(5U);
  }
}

/* 从1-Wire总线读取1位数据，返回0或1。 */
static uint8_t __DRIVER_SENSER_DS18B20ReadBit(void){
  uint32_t primask;
  uint8_t  bit;

  /* 拉低2µs启动读时隙，释放后10µs内采样，关中断保证采样时序 */
  primask = STDLIB_COMMON_EnterCritical();
  STDLIB_COMMON_GpioModeOutput(DS18B20_DQ_GPIO_ID, GPIO_LEVEL_LOW);
  STDLIB_DWT_DelayUs(2U);
  STDLIB_COMMON_GpioModeInput(DS18B20_DQ_GPIO_ID);
  STDLIB_DWT_DelayUs(10U);
  bit = STDLIB_COMMON_GpioRead(DS18B20_DQ_GPIO_ID);
  STDLIB_COMMON_ExitCritical(primask);

  /* 等待时隙剩余时间 */
  STDLIB_DWT_DelayUs(50U);
  return bit;
}

/* 向1-Wire总线写入1字节（LSB先发）。 */
static void __DRIVER_SENSER_DS18B20WriteByte(uint8_t data){
  uint8_t i;

  for(i = 0U; i < 8U; i++){
    __DRIVER_SENSER_DS18B20WriteBit(data & 0x01U);
    data >>= 1U;
  }
}

/* 从1-Wire总线读取1字节（LSB先收）。 */
static uint8_t __DRIVER_SENSER_DS18B20ReadByte(void){
  uint8_t i;
  uint8_t data = 0U;

  for(i = 0U; i < 8U; i++){
    if(__DRIVER_SENSER_DS18B20ReadBit() != 0U){
      data |= (uint8_t)(1U << i);
    }
  }
  return data;
}

/* 初始化DS18B20：写配置寄存器，将精度设置为9位（转换时间约94ms）。 */
void DRIVER_SENSER_DS18B20Init(void){
  if(__DRIVER_SENSER_DS18B20Reset() == 0U) return;
  __DRIVER_SENSER_DS18B20WriteByte(DS18B20_CMD_SKIP_ROM);
  __DRIVER_SENSER_DS18B20WriteByte(DS18B20_CMD_WRITE_SCRPAD);
  __DRIVER_SENSER_DS18B20WriteByte(0x00U);            /* TH寄存器 */
  __DRIVER_SENSER_DS18B20WriteByte(0x00U);            /* TL寄存器 */
  __DRIVER_SENSER_DS18B20WriteByte(DS18B20_CFG_9BIT); /* 配置寄存器：R1=0,R0=0,9位精度 */
}

/* 启动DS18B20温度转换，等待完成后读取暂存器，返回摄氏度。
 * 设备不存在或通信异常时返回 SENSER_DS18B20_TEMP_ERROR，且不更新senserInfo.ds18b20Temp。 */
float DRIVER_SENSER_DS18B20GetTemp(void){
  uint8_t  byte0;
  uint8_t  byte1;
  uint16_t raw;
  float    temp;

  /* 第一阶段：发送转换命令 */
  if(__DRIVER_SENSER_DS18B20Reset() == 0U){
    return SENSER_DS18B20_TEMP_ERROR;
  }
  __DRIVER_SENSER_DS18B20WriteByte(DS18B20_CMD_SKIP_ROM);
  __DRIVER_SENSER_DS18B20WriteByte(DS18B20_CMD_CONVERT_T);

  /* 等待9位精度转换完成（标称93.75ms，留余量至100ms） */
  vTaskDelay(pdMS_TO_TICKS(100U));

  /* 第二阶段：读取暂存器 */
  if(__DRIVER_SENSER_DS18B20Reset() == 0U){
    return SENSER_DS18B20_TEMP_ERROR;
  }
  __DRIVER_SENSER_DS18B20WriteByte(DS18B20_CMD_SKIP_ROM);
  __DRIVER_SENSER_DS18B20WriteByte(DS18B20_CMD_READ_SCRPAD);

  byte0 = __DRIVER_SENSER_DS18B20ReadByte(); /* 温度LSB */
  byte1 = __DRIVER_SENSER_DS18B20ReadByte(); /* 温度MSB */

  /* 9位有符号温度值转换，分辨率0.5℃/LSB（低3位为0） */
  raw  = ((uint16_t)byte1 << 8U) | (uint16_t)byte0;
  temp = (float)((int16_t)raw) / 16.0f;

  /* 仅在返回值有效时更新信息结构体，避免异常值覆盖上次有效读数 */
  if(temp >= DS18B20_TEMP_VALID_MIN){
    senserInfo.ds18b20Temp = temp;
  }
  return temp;
}

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
void DRIVER_SENSER_WaterTempCtrlInit(void){
  STDLIB_PID_Init(SENSER_DEP_WATER_TEMP_PID_ID,
                  WATER_TEMP_KP, WATER_TEMP_KI, WATER_TEMP_KD,
                  WATER_TEMP_P_MAX, WATER_TEMP_I_MAX, WATER_TEMP_OUT_MAX);
  senserInfo.waterTempCtrl.targetTemp = 0.0f;
}

/* 以senserInfo.ds18b20Temp为反馈，计算PID并同步设置两路加热模块占空比。
 * 加热器不具备制冷能力，PID负输出（水温超标）时占空比置零，依靠自然冷却。 */
void DRIVER_SENSER_WaterTempCtrlUpdate(float targetTemp){
  float    output;
  uint32_t duty;

  senserInfo.waterTempCtrl.targetTemp = targetTemp;

  output = STDLIB_PID_Calc(SENSER_DEP_WATER_TEMP_PID_ID,
                            targetTemp, senserInfo.ds18b20Temp);

  /* 负输出置零：加热器只能加热，超温时停止输出，自然冷却 */
  duty = (output > 0.0f) ? (uint32_t)output : 0U;

  DRIVER_SENSER_HeatSetDuty(SENSER_HEAT_CH_1, duty);
  DRIVER_SENSER_HeatSetDuty(SENSER_HEAT_CH_2, duty);
}
