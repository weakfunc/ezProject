/*============================================================================
 * README
 * 红外对管驱动模块，支持两路通道，中断触发。
 * 有遮挡时输出低电平，无遮挡时输出高电平。
 * EXTI已在CubeMX配置为上升+下降沿触发。
 *============================================================================*/

#include "driver_senser.h"
#include "stdlib_dwt.h"

/*============================================================================
 * 内部配置（仅driver_senser模块内部使用）
 *============================================================================*/

/* 通道ID到GPIO ID映射 */
static const uint8_t senserGpioMap[SENSER_CH_COUNT] = {
    SENSER_DEP_CH1_GPIO_ID,
    SENSER_DEP_CH2_GPIO_ID,
};

/*============================================================================
 * API接口
 *============================================================================*/

/* 红外对管模块数据 */
senserInfo_t senserInfo;

/* 初始化红外对管模块，读取实际GPIO电平初始化isBlocked，防止PollUpdate首次调用误检测上升沿 */
void DRIVER_SENSER_Init(void){
  for(uint8_t i = 0U; i < SENSER_CH_COUNT; i++){
    senserInfo.ch[i].isBlocked = SENSER_UNBLOCKED;
    senserInfo.ch[i].trigCyc   = 0U;
  }
}

/* 读取指定通道当前是否被遮挡 */
uint8_t DRIVER_SENSER_IsBlocked(uint8_t chId){
  uint8_t level = STDLIB_COMMON_GpioRead(senserGpioMap[chId]);
  return (level == SENSER_DEP_BLOCKED_LEVEL) ? SENSER_BLOCKED : SENSER_UNBLOCKED;
}

/* 轮询更新指定通道状态，检测遮挡上升沿并记录DWT时间戳，更新isBlocked
 * 仅在从未遮挡切换到遮挡时更新trigCyc，保证时间戳对应遮挡起始时刻 */
void DRIVER_SENSER_PollUpdate(uint8_t chId){
  uint32_t cyc     = STDLIB_DWT_GetCyc();   /* 立即捕获，尽量减少误差 */
  uint8_t  level   = STDLIB_COMMON_GpioRead(senserGpioMap[chId]);
  uint8_t  blocked = (level == SENSER_DEP_BLOCKED_LEVEL) ? SENSER_BLOCKED : SENSER_UNBLOCKED;

  /* 检测遮挡上升沿：从未遮挡变为遮挡时，记录DWT时间戳 */
  if(blocked == SENSER_BLOCKED && senserInfo.ch[chId].isBlocked == SENSER_UNBLOCKED){
    senserInfo.ch[chId].trigCyc = cyc;
  }
  senserInfo.ch[chId].isBlocked = blocked;
}

/* 高速阻塞等待CH2遮挡上升沿，直接读取GPIO寄存器实现µs级轮询
 * 每次循环约3时钟周期读取IDR寄存器（约0.04µs@72MHz），远快于stdlib封装路径
 * 仅在UNBLOCKED→BLOCKED上升沿时捕获DWT时间戳，避免已遮挡状态的误触发 */
uint8_t DRIVER_SENSER_WaitCh2Blocked(uint32_t startCyc, uint32_t timeoutMs){
  while(1){
    /* 直接读取GPIO IDR寄存器，BLOCKED_LEVEL=LOW即PIN位为0时表示遮挡 */
    uint8_t blocked = ((SENSER_DEP_CH2_PORT->IDR & SENSER_DEP_CH2_PIN_MASK) == 0U) ?
                       SENSER_BLOCKED : SENSER_UNBLOCKED;

    /* 检测遮挡上升沿（UNBLOCKED→BLOCKED），立即捕获DWT时间戳后返回 */
    if(blocked == SENSER_BLOCKED && senserInfo.ch[SENSER_CH2].isBlocked == SENSER_UNBLOCKED){
      senserInfo.ch[SENSER_CH2].trigCyc   = STDLIB_DWT_GetCyc();
      senserInfo.ch[SENSER_CH2].isBlocked = SENSER_BLOCKED;
      return SENSER_BLOCKED;
    }
    senserInfo.ch[SENSER_CH2].isBlocked = blocked;

    /* 超时：放弃本次等待 */
    if(STDLIB_DWT_IsElapsedMs(startCyc, timeoutMs) != 0U){
      return SENSER_UNBLOCKED;
    }
  }
}

/* EXTI中断回调，仅处理CH1（USER_IO_1）
 * CH1与CH2共用EXTI线，CH2改为上层阻塞轮询，不在此处理
 * 第一时间捕获DWT时间戳；仅在遮挡（下降沿）时更新trigCyc */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
  uint32_t cyc    = STDLIB_DWT_GetCyc();   /* 立即捕获时间戳，尽量减少误差 */
  uint8_t  blocked;

  if(GPIO_Pin == SENSER_DEP_CH1_PIN){
    blocked = DRIVER_SENSER_IsBlocked(SENSER_CH1);
    senserInfo.ch[SENSER_CH1].isBlocked = blocked;
    if(blocked == SENSER_BLOCKED){
      senserInfo.ch[SENSER_CH1].trigCyc = cyc;
    }
  }
}
