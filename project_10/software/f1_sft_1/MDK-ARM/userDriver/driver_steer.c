/*============================================================================
 * README
 *
 *============================================================================*/

#include "driver_steer.h"
#include "cmsis_os.h"

/*============================================================================
 * 内部配置（仅driver_steer模块内部使用）
 *============================================================================*/

/* 舵机资源映射结构体 */
typedef struct {
  uint8_t pwmCh;
} steerMap_t;

/* 舵机默认PWM通道映射表 */
static const steerMap_t steerMap[STEER_SERVO_COUNT] = {
  [STEER_SERVO_1] = { STEER_DEP_SERVO_1_PWM_CH },
  [STEER_SERVO_2] = { STEER_DEP_SERVO_2_PWM_CH },
};

/* 舵机模块数据 */
steerInfo_t steerInfo[STEER_SERVO_COUNT] = {
  {.ch = 0, .duty = 1000, .dir = STEER_DIR_CW, .timeMs = STEER_360_AGNLE_90},
  {.ch = 1, .duty = 1000, .dir = STEER_DIR_CW, .timeMs = STEER_360_AGNLE_90},
};

/*============================================================================
 * API接口
 *============================================================================*/

/* 360度舵机旋转控制：按指定方向旋转指定时间后停止 */
void DRIVER_STEER_Rotate360(uint8_t steerId, uint8_t dir, uint32_t timeMs){
  uint16_t pulse;

  if(steerId >= STEER_SERVO_COUNT){
    return;
  }

  /* 根据方向选择脉宽 */
  pulse = (dir == STEER_DIR_CW) ? STEER_360_CW : STEER_360_CCW;

  STDLIB_TIM_PwmSetPulse(steerMap[steerId].pwmCh, pulse);
  osDelay(timeMs);
  STDLIB_TIM_PwmSetPulse(steerMap[steerId].pwmCh, STEER_360_STOP);
}

/* 180度舵机角度控制：根据角度宏将默认CCR输出到对应PWM通道 */
void DRIVER_STEER_Rotate180(uint8_t steerId, uint16_t anglePulse){
  if(steerId >= STEER_SERVO_COUNT){
    return;
  }

  STDLIB_TIM_PwmSetPulse(steerMap[steerId].pwmCh, anglePulse);
}

void DRIVER_STEER_DebugMode(void){
  for(int i = 0; i < STEER_SERVO_COUNT; i++){
    //DRIVER_STEER_Rotate180(steerInfo[i].ch, steerInfo[i].duty);
    DRIVER_STEER_Rotate360(steerInfo[i].ch, steerInfo[i].dir, steerInfo[i].timeMs);
  }
}
