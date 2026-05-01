/*============================================================================
 * README
 * DRIVER_STEER_Updata() 360度舵机状态非阻塞更新
 *============================================================================*/

#include "driver_steer.h"
#include "stdlib_dwt.h"

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

/* 单条旋转指令 */
typedef struct {
  uint8_t  dir;    /* 旋转方向：STEER_DIR_CW / STEER_DIR_CCW */
  uint32_t timeMs; /* 旋转时长（ms） */
} steerCmdItem_t;

/* 单路舵机指令队列（循环队列） */
typedef struct {
  steerCmdItem_t items[STEER_CMD_QUEUE_LEN]; /* 指令缓冲 */
  uint8_t        head;                        /* 出队索引 */
  uint8_t        tail;                        /* 入队索引 */
  uint8_t        cnt;                         /* 当前队列中指令数 */
} steerCmdQueue_t;

/* 各路舵机的指令队列 */
static steerCmdQueue_t steerCmdQueue[STEER_SERVO_COUNT];

/* 各路舵机当前指令起始DWT周期值 */
static uint32_t steerStartCyc[STEER_SERVO_COUNT];

/* 舵机模块数据（公有，extern在.h中声明） */
steerInfo_t steerInfo[STEER_SERVO_COUNT];

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 启动队列中下一条指令；队列为空则停止输出并清isRunning */
static void __DRIVER_STEER_StartNext(uint8_t steerId){
  steerCmdQueue_t *q = &steerCmdQueue[steerId];
  steerCmdItem_t  *cmd;
  uint16_t         pulse;

  if(q->cnt == 0U){
    /* 队列已空，输出停止脉宽 */
    STDLIB_TIM_PwmSetPulse(steerMap[steerId].pwmCh, STEER_360_STOP);
    steerInfo[steerId].isRunning = 0U;
    steerInfo[steerId].queueCnt  = 0U;
    return;
  }

  /* 取出队头指令 */
  cmd = &q->items[q->head];
  q->head = (uint8_t)((q->head + 1U) % STEER_CMD_QUEUE_LEN);
  q->cnt--;

  pulse = (cmd->dir == STEER_DIR_CW) ? STEER_360_CW : STEER_360_CCW;
  STDLIB_TIM_PwmSetPulse(steerMap[steerId].pwmCh, pulse);

  steerInfo[steerId].isRunning = 1U;
  steerInfo[steerId].dir       = cmd->dir;
  steerInfo[steerId].timeMs    = cmd->timeMs;
  steerInfo[steerId].queueCnt  = q->cnt;

  steerStartCyc[steerId] = STDLIB_DWT_GetCyc();
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 180度舵机角度控制：根据角度宏将脉宽输出到对应PWM通道 */
void DRIVER_STEER_Rotate180(uint8_t steerId, uint16_t anglePulse){
  STDLIB_TIM_PwmSetPulse(steerMap[steerId].pwmCh, anglePulse);
}

/* 360度舵机非阻塞旋转：将指令入队，立即返回；timeMs=0时立即停止并清空队列 */
void DRIVER_STEER_Rotate360(uint8_t steerId, uint8_t dir, uint32_t timeMs){
  steerCmdQueue_t *q = &steerCmdQueue[steerId];

  if(timeMs == 0U){
    /* 立即停止，清空队列 */
    q->head = 0U;
    q->tail = 0U;
    q->cnt  = 0U;
    STDLIB_TIM_PwmSetPulse(steerMap[steerId].pwmCh, STEER_360_STOP);
    steerInfo[steerId].isRunning = 0U;
    steerInfo[steerId].queueCnt  = 0U;
    return;
  }

  /* 队列已满则丢弃 */
  if(q->cnt >= STEER_CMD_QUEUE_LEN){
    return;
  }

  /* 入队 */
  q->items[q->tail].dir    = dir;
  q->items[q->tail].timeMs = timeMs;
  q->tail = (uint8_t)((q->tail + 1U) % STEER_CMD_QUEUE_LEN);
  q->cnt++;

  /* 若当前空闲则立即启动 */
  if(steerInfo[steerId].isRunning == 0U){
    __DRIVER_STEER_StartNext(steerId);
  } else {
    steerInfo[steerId].queueCnt = q->cnt;
  }
}

/* 非阻塞旋转状态更新，需在task层每2ms周期调用 */
void DRIVER_STEER_Updata(void){
  uint8_t i;

  for(i = 0U; i < STEER_SERVO_COUNT; i++){
    if(steerInfo[i].isRunning == 0U){
      continue;
    }
    if(STDLIB_DWT_IsElapsedMs(steerStartCyc[i], steerInfo[i].timeMs) != 0U){
      __DRIVER_STEER_StartNext(i);
    }
  }
}

void DRIVER_STEER_DebugMode(void){
  uint8_t i;

  for(i = 0U; i < STEER_SERVO_COUNT; i++){
    DRIVER_STEER_Rotate360(i, STEER_DIR_CW, STEER_360_AGNLE_90);
  }
}
