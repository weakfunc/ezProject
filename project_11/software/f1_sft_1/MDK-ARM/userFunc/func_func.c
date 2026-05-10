/*============================================================================
 * README
 * 发射器功能模块：实现按键选速、PWM占空比控制及OLED界面绘制
 *============================================================================*/

#include "func_func.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_senser.h"
#include "stdlib_tim.h"
#include "stdlib_dwt.h"
#include "stdlib_pid.h"
#include "FreeRTOS.h"
#include "task.h"

/*============================================================================
 * 内部配置（仅func_func模块内部使用）
 *============================================================================*/

/* UI布局参数（OLED 128×64）
 * 中部大字显示固定目标速度"5m/s"，右侧小字显示质量；
 * 下方大字显示红外实测速度。
 */
#define FUNC_TARGET_SPEED_MS     (5.0f)
#define FUNC_UI_TARGET_VAL_X     (40U)   /* "5m/s" 12x16居中起始x坐标 */
#define FUNC_UI_TARGET_VAL_Y     (16U)   /* 目标速度位于屏幕中部 */
#define FUNC_UI_MASS_X           (98U)   /* 质量显示在目标速度右侧 */
#define FUNC_UI_MASS_Y           (20U)
#define FUNC_UI_ACTUAL_LABEL_Y   (38U)   /* 实测速度标签y坐标 */
#define FUNC_UI_ACTUAL_VAL_X     (28U)   /* "0.0m/s" 12x16居中起始x坐标 */
#define FUNC_UI_ACTUAL_VAL_Y     (48U)   /* 实测速度数值y坐标 */

/* 各按键上次pressCount缓存，用于检测按键上升沿 */
static uint32_t funcKeyLastPressCount[BOARD_KEY_COUNT];

/* 测速状态机状态枚举 */
typedef enum {
    SPEED_MEAS_IDLE   = 0U,   /* 等待对管1触发上升沿 */
    SPEED_MEAS_TIMING = 1U,   /* 对管1已触发，等待对管2触发上升沿 */
} funcSpeedMeasState_e;

/* 对管间距（mm），v = SENSER_DIST_MM / elapsedUs × 1000 [m/s] */
#define FUNC_SENSER_DIST_MM        (60U)

/* 测速超时时长（ms），超时后重置状态机，保留上次有效测速结果 */
#define FUNC_SPEED_MEAS_TIMEOUT_MS (2000U)

/* 有效测速后清空延时（ms），超时后清空所有测速参数并归零actualSpeed */
#define FUNC_SPEED_CLEAR_DELAY_MS  (5000U)

/* 测速结果有效下限（m/s），低于此值视为噪声，不更新actualSpeed */
#define FUNC_SPEED_MEAS_MIN_MS     (0.5f)

/* 速度融合容差（比例），测速结果须在目标速度±10%以内才接受，否则视为无效 */
#define FUNC_SPEED_FUSION_TOL      (0.1f)

/* 测速上下文结构体（所有中间变量集中在此，便于调试器整体查看）
 * CH1中断触发记录startCyc，CH2阻塞轮询捕获endCyc，DWT计算飞行时间 */
typedef struct {
    funcSpeedMeasState_e state;          /* 当前状态（便于调试观察） */
    uint32_t             startCyc;       /* 对管1触发时DWT周期计数（由ISR写入trigCyc） */
    uint32_t             lastCh1TrigCyc; /* 上次记录的对管1 trigCyc（检测新触发） */
    uint32_t             endCyc;         /* 对管2检测到遮挡时的DWT周期计数 */
    uint32_t             elapsedCyc;     /* 飞行DWT周期数 */
    float                elapsedUs;      /* 飞行时间（us） */
    float                speed;          /* 本次计算速度（m/s） */
    uint8_t              clearPending;   /* 是否等待5s后清空：1=等待中，0=未启动 */
    uint32_t             clearStartCyc; /* 有效测速完成时DWT周期计数，用于5s计时 */
} funcSpeedMeasCtx_t;

/* 测速上下文实例（不加static，便于调试器查看） */
funcSpeedMeasCtx_t funcSpeedMeasCtx;

/* 占空比-质量显示映射表 */
typedef struct {
    uint8_t duty;         /* 占空比（%） */
    uint8_t massG;        /* 对应质量（g） */
} funcDutyMassMap_t;

static const funcDutyMassMap_t funcDutyMassMap[BOARD_KEY_COUNT] = {
    {SPEED_5MS, 50U},   /* KEY1：100% -> 50g */
    {SPEED_3MS, 40U},   /* KEY2：95%  -> 40g */
    {SPEED_1MS, 30U},   /* KEY3：90%  -> 30g */
};

static uint8_t FUNC_FUNC_GetMassG(uint8_t duty){
  for(uint8_t i = 0U; i < BOARD_KEY_COUNT; i++){
    if(funcDutyMassMap[i].duty == duty){
      return funcDutyMassMap[i].massG;
    }
  }

  return 0U;
}

/*============================================================================
 * 向上提供API
 *============================================================================*/

/* 发射器功能模块数据 */
funcInfo_t funcInfo;

/* 功能模块初始化：默认目标速度5m/s，启动PWM，重置测速状态机 */
void FUNC_FUNC_Init(void){
  funcInfo.targetDuty  = SPEED_5MS;
  funcInfo.targetSpeed = FUNC_TARGET_SPEED_MS;
  funcInfo.actualSpeed = 0.0f;
  funcInfo.measError   = 0U;

  for(uint8_t i = 0U; i < BOARD_KEY_COUNT; i++){
    funcKeyLastPressCount[i] = 0U;
  }

  funcSpeedMeasCtx.state          = SPEED_MEAS_IDLE;
  funcSpeedMeasCtx.startCyc       = 0U;
  funcSpeedMeasCtx.lastCh1TrigCyc = 0U;
  funcSpeedMeasCtx.endCyc         = 0U;
  funcSpeedMeasCtx.elapsedCyc     = 0U;
  funcSpeedMeasCtx.elapsedUs      = 0.0f;
  funcSpeedMeasCtx.speed          = 0.0f;
  funcSpeedMeasCtx.clearPending   = 0U;
  funcSpeedMeasCtx.clearStartCyc  = 0U;

  STDLIB_TIM_PwmSetDuty(PWM_TIM2_CH1, (float)SPEED_5MS);
}

/* 按键扫描更新：检测上升沿，更新目标速度及PWM占空比，需周期调用 */
void FUNC_FUNC_KeyUpdate(void){
  for(uint8_t i = 0U; i < BOARD_KEY_COUNT; i++){
    if(boardInfo.key[i].pressCount != funcKeyLastPressCount[i]){
      funcKeyLastPressCount[i] = boardInfo.key[i].pressCount;
      funcInfo.targetDuty      = funcDutyMassMap[i].duty;
      funcInfo.targetSpeed     = FUNC_TARGET_SPEED_MS;
      STDLIB_TIM_PwmSetDuty(PWM_TIM2_CH1, (float)funcDutyMassMap[i].duty);
    }
  }
}

/* 红外对管测速更新，检测对管触发上升沿并以DWT计算飞行时间，需周期调用
 * 状态机：IDLE --(对管1上升沿)--> TIMING --(对管2上升沿)--> IDLE（更新actualSpeed）
 * 超时（FUNC_SPEED_MEAS_TIMEOUT_MS）后重置为IDLE，保留上次有效测速值 */
/* 红外对管测速更新，需周期调用。
 * CH1由中断触发并记录trigCyc；检测到CH1新触发后，
 * 阻塞忙等（DWT）CH2被遮挡，超时时长FUNC_SPEED_MEAS_TIMEOUT_MS。*/
void FUNC_FUNC_SpeedMeasUpdate(void){
  /* 5s清空计时：有效测速后等待5s，超时后清空所有参数并归零actualSpeed */
  if(funcSpeedMeasCtx.clearPending != 0U){
    if(STDLIB_DWT_IsElapsedMs(funcSpeedMeasCtx.clearStartCyc, FUNC_SPEED_CLEAR_DELAY_MS) != 0U){
      /* 清空全部测速上下文参数 */
      funcSpeedMeasCtx.state          = SPEED_MEAS_IDLE;
      funcSpeedMeasCtx.startCyc       = 0U;
      funcSpeedMeasCtx.lastCh1TrigCyc = senserInfo.ch[SENSER_CH1].trigCyc; /* 同步当前trigCyc，防止立即重触发 */
      funcSpeedMeasCtx.endCyc         = 0U;
      funcSpeedMeasCtx.elapsedCyc     = 0U;
      funcSpeedMeasCtx.elapsedUs      = 0.0f;
      funcSpeedMeasCtx.speed          = 0.0f;
      funcSpeedMeasCtx.clearPending   = 0U;
      funcSpeedMeasCtx.clearStartCyc  = 0U;
      funcInfo.actualSpeed            = 0.0f;
      funcInfo.measError              = 0U;
      /* 同步CH2当前GPIO状态，使isBlocked与实际电平一致，便于调试观察 */
      DRIVER_SENSER_PollUpdate(SENSER_CH2);
    }
    return; /* 5s等待期间阻止新的测速 */
  }

  /* 无新触发则立即返回 */
  if(senserInfo.ch[SENSER_CH1].trigCyc == funcSpeedMeasCtx.lastCh1TrigCyc){
    return;
  }

  /* 记录本次CH1触发时间戳（ISR已精确捕获） */
  funcSpeedMeasCtx.lastCh1TrigCyc = senserInfo.ch[SENSER_CH1].trigCyc;
  funcSpeedMeasCtx.startCyc       = senserInfo.ch[SENSER_CH1].trigCyc;
  funcSpeedMeasCtx.state          = SPEED_MEAS_TIMING;

  /* 进入高速轮询前同步CH2初始状态，防止已有遮挡被误识别为新触发 */
  DRIVER_SENSER_PollUpdate(SENSER_CH2);

  /* 暂停FreeRTOS调度器，防止任务切换打断µs级轮询导致漏检
   * 注：硬件中断（含DWT）在vTaskSuspendAll期间仍正常工作 */
  vTaskSuspendAll();
  uint8_t ch2Result = DRIVER_SENSER_WaitCh2Blocked(funcSpeedMeasCtx.startCyc,
                                                    FUNC_SPEED_MEAS_TIMEOUT_MS);
  xTaskResumeAll();

  if(ch2Result == SENSER_BLOCKED){
    funcSpeedMeasCtx.endCyc     = senserInfo.ch[SENSER_CH2].trigCyc;
    funcSpeedMeasCtx.elapsedCyc = funcSpeedMeasCtx.endCyc - funcSpeedMeasCtx.startCyc;
    funcSpeedMeasCtx.elapsedUs  = (float)funcSpeedMeasCtx.elapsedCyc / (float)dwtInfo.cycPerUs;
    if(funcSpeedMeasCtx.elapsedUs > 0.0f){
      /* v(m/s) = 间距(mm) / 飞行时间(us) × 1000 */
      funcSpeedMeasCtx.speed = (float)FUNC_SENSER_DIST_MM * 1000.0f / funcSpeedMeasCtx.elapsedUs;
      /* 速度融合：须高于最小有效速度才写入显示；超出目标速度±10%容差则标注测量错误 */
      if(funcSpeedMeasCtx.speed >= FUNC_SPEED_MEAS_MIN_MS){
        float tolLow  = funcInfo.targetSpeed * (1.0f - FUNC_SPEED_FUSION_TOL);
        float tolHigh = funcInfo.targetSpeed * (1.0f + FUNC_SPEED_FUSION_TOL);
        funcInfo.actualSpeed          = funcSpeedMeasCtx.speed;
        funcInfo.measError            = (funcSpeedMeasCtx.speed < tolLow ||
                                         funcSpeedMeasCtx.speed > tolHigh) ? 1U : 0U;
        /* 启动5s清空计时 */
        funcSpeedMeasCtx.clearPending  = 1U;
        funcSpeedMeasCtx.clearStartCyc = STDLIB_DWT_GetCyc();
      }
    }
  }

  funcSpeedMeasCtx.state = SPEED_MEAS_IDLE;
}

/* 刷新OLED显存：固定目标速度+质量+实测速度，需配合Refresh()使用 */
void FUNC_FUNC_DrawUI(void){
  uint8_t xPos;
  uint8_t massG;

  /* 目标速度固定显示为5m/s，右侧显示当前占空比对应质量。 */
  xPos = DRIVER_OLED_ShowFloat12x16(FUNC_UI_TARGET_VAL_X, FUNC_UI_TARGET_VAL_Y, funcInfo.targetSpeed, 0U);
  DRIVER_OLED_ShowString12x16(xPos, FUNC_UI_TARGET_VAL_Y, "m/s");

  massG = FUNC_FUNC_GetMassG(funcInfo.targetDuty);
  if(massG == 0U){
    DRIVER_OLED_ShowString(FUNC_UI_MASS_X, FUNC_UI_MASS_Y, "--g");
  } else {
    DRIVER_OLED_ShowNum(FUNC_UI_MASS_X, FUNC_UI_MASS_Y, massG, 2U);
    DRIVER_OLED_ShowString((uint8_t)(FUNC_UI_MASS_X + 12U), FUNC_UI_MASS_Y, "g");
  }

  /* 下方直接显示红外测速得到的实测速度。 */
  DRIVER_OLED_ShowString(0U, FUNC_UI_ACTUAL_LABEL_Y, "Actual:");
  xPos = DRIVER_OLED_ShowFloat12x16(FUNC_UI_ACTUAL_VAL_X, FUNC_UI_ACTUAL_VAL_Y, funcInfo.actualSpeed, 1U);
  DRIVER_OLED_ShowString12x16(xPos, FUNC_UI_ACTUAL_VAL_Y, "m/s");
}
