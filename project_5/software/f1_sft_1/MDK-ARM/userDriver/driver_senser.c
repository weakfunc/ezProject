#include "driver_senser.h"

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 传感器模块信息（公有，供上层直接访问）。 */
senserInfo_t senserInfo;

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化传感器驱动：将 TRIG 引脚置低，准备 HC-SR04 触发。 */
void DRIVER_SENSER_Init(void){
  SENSER_HCSR04_DEP_TRIG_SET(GPIO_LEVEL_LOW);
}

/* HC-SR04：发送 10us 触发脉冲，阻塞测量回波时间并计算距离。
 * 时序：TRIG 低 2us → TRIG 高 10us → TRIG 低 → 等待 ECHO 高 → 计时 → 等待 ECHO 低 → 计算距离。
 * 距离公式：distMm = echoUs × 17 / 100（声速 340m/s，单程距离）。
 * distMm : 输出参数，NULL 时仅更新 senserInfo；
 * 成功返回 1，等待超时返回 0。
 */
uint8_t DRIVER_SENSER_GetHCSR04Distance(uint16_t *distMm){
  uint32_t startCyc, echoStartCyc, echoEndCyc, elapsedUs;

  /* 确保 TRIG 初始为低，等待 2us */
  SENSER_HCSR04_DEP_TRIG_SET(GPIO_LEVEL_LOW);
  SENSER_HCSR04_DEP_DELAY_US(2U);

  /* 发送 10us 触发脉冲 */
  SENSER_HCSR04_DEP_TRIG_SET(GPIO_LEVEL_HIGH);
  SENSER_HCSR04_DEP_DELAY_US(10U);
  SENSER_HCSR04_DEP_TRIG_SET(GPIO_LEVEL_LOW);

  /* 等待 ECHO 上升沿（传感器开始发超声波） */
  startCyc = SENSER_HCSR04_DEP_GET_CYC();
  while(SENSER_HCSR04_DEP_ECHO_READ() == GPIO_LEVEL_LOW){
    if(SENSER_HCSR04_DEP_ELAPSED_US(startCyc, SENSER_HCSR04_START_TIMEOUT_US)){
      return 0U;
    }
  }

  /* 等待 ECHO 下降沿并计时（超声波返回） */
  echoStartCyc = SENSER_HCSR04_DEP_GET_CYC();
  while(SENSER_HCSR04_DEP_ECHO_READ() == GPIO_LEVEL_HIGH){
    if(SENSER_HCSR04_DEP_ELAPSED_US(echoStartCyc, SENSER_HCSR04_ECHO_TIMEOUT_US)){
      return 0U;
    }
  }
  echoEndCyc = SENSER_HCSR04_DEP_GET_CYC();

  /* 计算 ECHO 高电平持续时间（微秒） */
  elapsedUs = (echoEndCyc - echoStartCyc) / dwtInfo.cycPerUs;

  /* 换算距离：单程 = 总时间 / 2，距离(mm) = 时间(us) × 17 / 100 */
  senserInfo.hcsrDistMm  = (uint16_t)(elapsedUs * 17U / 100U);
  senserInfo.hcsrIsValid = 1U;

  if(distMm != NULL){
    *distMm = senserInfo.hcsrDistMm;
  }
  return 1U;
}
