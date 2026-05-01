#include "task_user1.h"
#include "task_system.h"
#include "driver_oled.h"
#include "driver_steer.h"
#include "driver_qr.h"
#include "func_appcom.h"
#include "func_leadScrew.h"
#include "driver_stepperMotor.h"

/*============================================================================
 * 用户配置
 *============================================================================*/

/* 识别到目标后等待时间：user1任务周期 2ms，1000次 = 2s。 */
#define SORT_WAIT_TICKS            1000U

/* 分拣舵机动作时间，需按现场舵机速度继续标定。 */
#define SORT_SERVO_PUSH_MS         400U
#define SORT_SERVO_RETURN_MS       390U

/* 当前使用一个360度舵机执行分拣动作。 */
#define SORT_SERVO_ID              STEER_SERVO_1

/* 二维码目标地点数量，编码支持 0x00~0x09 或 ASCII '0'~'9'。 */
#define SORT_TARGET_COUNT          10U
#define SORT_TARGET_INVALID        0xFFU

/* 电机保护阈值。 */
#define SORT_PROTECT_TEMP_C        30
#define SORT_PROTECT_CURRENT_A     1.2f

/*============================================================================
 * 私有类型定义
 *============================================================================*/

/* 分拣状态机状态。 */
typedef enum {
  SORT_STATE_WAIT_QR = 0,       /* 等待二维码识别 */
  SORT_STATE_WAIT_DELAY,        /* 识别后等待2s */
  SORT_STATE_MOVE_TO_TARGET,    /* 丝杆移动到目标地点 */
  SORT_STATE_SERVO_ACTION,      /* 舵机执行分拣动作 */
  SORT_STATE_RETURN_HOME,       /* 丝杆返回原点 */
} sortState_e;

/* 电机保护状态。 */
typedef enum {
  SORT_PROTECT_NONE = 0,       /* 无保护 */
  SORT_PROTECT_OVERTEMP,       /* 超温保护 */
  SORT_PROTECT_OVERCURRENT,    /* 过流保护 */
} sortProtectState_e;

/* 单个目标地点配置。 */
typedef struct {
  float   posCm;       /* 丝杆目标位置（cm，相对原点） */
  uint8_t servoDir;    /* 分拣方向：STEER_DIR_CW / STEER_DIR_CCW */
} sortTargetCfg_t;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* 目标地点配置变量表。调试标定时可在 Watch 中修改 sortTargetCfg[x].posCm / servoDir。 */
sortTargetCfg_t sortTargetCfg[SORT_TARGET_COUNT] = {
  {  3.0f, STEER_DIR_CCW },  /* 目标地点1，QR=0x00 */
  {  3.0f, STEER_DIR_CW  },  /* 目标地点2，QR=0x01 */
  { 10.0f, STEER_DIR_CCW },  /* 目标地点3，QR=0x02 */
  { 10.0f, STEER_DIR_CW  },  /* 目标地点4，QR=0x03 */
  { 17.0f, STEER_DIR_CCW },  /* 目标地点5，QR=0x04 */
  { 17.0f, STEER_DIR_CW  },  /* 目标地点6，QR=0x05 */
  { 24.0f, STEER_DIR_CCW },  /* 目标地点7，QR=0x06 */
  { 24.0f, STEER_DIR_CW  },  /* 目标地点8，QR=0x07 */
  { 31.0f, STEER_DIR_CCW },  /* 目标地点9，QR=0x08 */
  { 31.0f, STEER_DIR_CW  },  /* 目标地点10，QR=0x09 */
};

/* 当前分拣状态机运行变量。 */
static sortState_e sSortState;
static uint8_t     sCurrentTargetIdx;
static uint32_t    sStateStartTick;
static float       sLeadScrewTargetCm;
static uint8_t     sLeadScrewCmdPending;
static uint8_t     sStepperStopPending;
static sortProtectState_e sProtectState;

/* 最近一次有效的QR数据值，初始化为0xFF（未收到数据）。 */
static uint8_t lastValidObj1 = 0xFFU;

/* QR模块有效数据包累计计数。 */
static uint32_t qrLocalPktCnt = 0U;

user1TaskInfo_t user1TaskInfo;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 将二维码字节解析为目标配置表下标。
 * 支持二进制 0x00~0x09，也兼容 ASCII '0'~'9'。
 */
static uint8_t __decodeTargetIdx(uint8_t code) {
  if(code < SORT_TARGET_COUNT) {
    return code;
  }
  if(code >= '0' && code <= '9') {
    return (uint8_t)(code - '0');
  }
  return SORT_TARGET_INVALID;
}

/* 触发一次完整舵机动作。
 * 顺时针动作：先CCW转出，再CW回原位；逆时针动作反向执行。
 */
static void __startSortServoAction(uint8_t servoDir) {
  if(servoDir == STEER_DIR_CW) {
    DRIVER_STEER_Rotate360(SORT_SERVO_ID, STEER_DIR_CCW, SORT_SERVO_PUSH_MS);
    DRIVER_STEER_Rotate360(SORT_SERVO_ID, STEER_DIR_CW,  SORT_SERVO_RETURN_MS);
  } else {
    DRIVER_STEER_Rotate360(SORT_SERVO_ID, STEER_DIR_CW,  SORT_SERVO_PUSH_MS);
    DRIVER_STEER_Rotate360(SORT_SERVO_ID, STEER_DIR_CCW, SORT_SERVO_RETURN_MS);
  }
}

/* 舵机队列是否已经执行完成。 */
static uint8_t __isSortServoIdle(void) {
  return (steerInfo[SORT_SERVO_ID].isRunning == 0U &&
          steerInfo[SORT_SERVO_ID].queueCnt  == 0U);
}

/* 提交丝杆绝对位置请求，实际控制命令在 DRIVER_STEPPER_Axis0CtrlHook 中发送。 */
static void __requestLeadScrewAbsPos(float targetCm) {
  sLeadScrewTargetCm   = targetCm;
  sLeadScrewCmdPending = 1U;
}

/* 丝杆请求已被钩子消费，且运动已经结束。 */
static uint8_t __isLeadScrewAbsPosDone(void) {
  return (sLeadScrewCmdPending == 0U && leadScrewInfo.state == LEADSCRW_STATE_IDLE);
}

/* 检查电机温度/相电流保护，保护触发后锁定并通过钩子发送停止命令。 */
static void __updateMotorProtection(void) {
  stepperMotorInfo_t *info = &STEPPER_INFO(LEADSCRW_MOTOR_ID);

  if(sProtectState != SORT_PROTECT_NONE) {
    return;
  }

  if(info->driverTemp > SORT_PROTECT_TEMP_C) {
    sProtectState = SORT_PROTECT_OVERTEMP;
  } else if(info->phaseCurrent_A > SORT_PROTECT_CURRENT_A) {
    sProtectState = SORT_PROTECT_OVERCURRENT;
  } else {
    return;
  }

  sLeadScrewCmdPending = 0U;
  sStepperStopPending  = 1U;
  qrInfo.hasNewData    = 0U;
}

/* 分拣状态机。 */
static void sortStateMachine(void) {
  uint8_t targetIdx;

  if(sProtectState != SORT_PROTECT_NONE) {
    qrInfo.hasNewData = 0U;
    return;
  }

  switch(sSortState) {
    case SORT_STATE_WAIT_QR:
      if(qrInfo.hasNewData == 0U) {
        return;
      }

      qrInfo.hasNewData = 0U;
      targetIdx = __decodeTargetIdx(qrInfo.data1);
      if(targetIdx == SORT_TARGET_INVALID) {
        lastValidObj1 = qrInfo.data1;
        return;
      }

      sCurrentTargetIdx = targetIdx;
      sStateStartTick   = user1TaskInfo.user1TaskCnt;
      lastValidObj1     = qrInfo.data1;
      qrLocalPktCnt++;
      sSortState = SORT_STATE_WAIT_DELAY;
      break;

    case SORT_STATE_WAIT_DELAY:
      if((uint32_t)(user1TaskInfo.user1TaskCnt - sStateStartTick) >= SORT_WAIT_TICKS) {
        __requestLeadScrewAbsPos(sortTargetCfg[sCurrentTargetIdx].posCm);
        sSortState = SORT_STATE_MOVE_TO_TARGET;
      }
      break;

    case SORT_STATE_MOVE_TO_TARGET:
      if(__isLeadScrewAbsPosDone() != 0U) {
        __startSortServoAction(sortTargetCfg[sCurrentTargetIdx].servoDir);
        sSortState = SORT_STATE_SERVO_ACTION;
      }
      break;

    case SORT_STATE_SERVO_ACTION:
      if(__isSortServoIdle() != 0U) {
        __requestLeadScrewAbsPos(0.0f);
        sSortState = SORT_STATE_RETURN_HOME;
      }
      break;

    case SORT_STATE_RETURN_HOME:
      if(__isLeadScrewAbsPosDone() != 0U) {
        sSortState = SORT_STATE_WAIT_QR;
      }
      break;

    default:
      sSortState = SORT_STATE_WAIT_QR;
      break;
  }

  if(sSortState != SORT_STATE_WAIT_QR && qrInfo.hasNewData != 0U) {
    qrInfo.hasNewData = 0U;
  }
}

/* 查询obj1编码对应的显示标签。 */
static const char* __getObj1Label(uint8_t obj1) {
  static const char *labelTable[SORT_TARGET_COUNT] = {
    "T01", "T02", "T03", "T04", "T05",
    "T06", "T07", "T08", "T09", "T10",
  };
  uint8_t targetIdx;

  if(obj1 == 0xFFU) {
    return "NONE";
  }

  targetIdx = __decodeTargetIdx(obj1);
  if(targetIdx < SORT_TARGET_COUNT) {
    return labelTable[targetIdx];
  }
  return "UNKN";
}

/* 查询状态机对应的OLED显示标签。 */
static const char* __getStateLabel(sortState_e state) {
  if(sProtectState == SORT_PROTECT_OVERTEMP) {
    return "OTMP";
  }
  if(sProtectState == SORT_PROTECT_OVERCURRENT) {
    return "OCUR";
  }

  switch(state) {
    case SORT_STATE_WAIT_QR:        return "WAIT";
    case SORT_STATE_WAIT_DELAY:     return "DLY ";
    case SORT_STATE_MOVE_TO_TARGET: return "MOVE";
    case SORT_STATE_SERVO_ACTION:   return "SORT";
    case SORT_STATE_RETURN_HOME:    return "HOME";
    default:                        return "ERR ";
  }
}

/* OLED显示刷新：先将上一帧推送到屏幕，再更新显存。 */
static void oledControl(void) {
  DRIVER_OLED_Refresh();

  DRIVER_OLED_ShowString(5,  0,  "time:");
  DRIVER_OLED_ShowNum(35, 0,  systemTaskInfo.systemTaskCnt / 100U, 4);
  DRIVER_OLED_DrawPoint(60, 5,  1U);
  DRIVER_OLED_ShowNum(63, 0,  (systemTaskInfo.systemTaskCnt / 10U) % 10U, 1);
  DRIVER_OLED_ShowString(72, 0,  "s");

  if(systemTaskInfo.systemEnable_board) {
    DRIVER_OLED_ShowString(5,  11, "SYS: ENABLE ");
  } else {
    DRIVER_OLED_ShowString(5,  11, "SYS: DISABLE");
  }

  DRIVER_OLED_ShowString(5,  22, "ST: ");
  DRIVER_OLED_ShowString(35, 22, __getStateLabel(sSortState));

  DRIVER_OLED_ShowString(5,  33, "QR: ");
  DRIVER_OLED_ShowString(35, 33, __getObj1Label(lastValidObj1));
  // DRIVER_OLED_ShowString(62, 33, "RX:");
  // DRIVER_OLED_ShowNum(86, 33, qrInfo.rxTotalCnt, 4);

  DRIVER_OLED_ShowString(5,  44, "POS:");
  DRIVER_OLED_ShowFloat(35, 44, leadScrewInfo.currentPos_cm, 1);
  DRIVER_OLED_ShowString(72, 44, "cm");

  DRIVER_OLED_ShowString(5,  55, "CNT:");
  DRIVER_OLED_ShowNum(35, 55, qrInfo.rxTotalCnt, 5);
}

void DRIVER_STEPPER_Axis0CtrlHook(void) {
  if(sStepperStopPending != 0U) {
    DRIVER_STEPPER_Stop(LEADSCRW_MOTOR_ID);
    sStepperStopPending = 0U;
    return;
  }

  if(sProtectState != SORT_PROTECT_NONE) {
    return;
  }

  if(sLeadScrewCmdPending != 0U && leadScrewInfo.state == LEADSCRW_STATE_IDLE) {
    FUNC_LEADSCREW_SetAbsPos(sLeadScrewTargetCm);
    sLeadScrewCmdPending = 0U;
  }
}

/*============================================================================
 * 任务入口
 *============================================================================*/

void user1TaskInit(void) {
  DRIVER_QR_Init();

  osDelay(100);
  DRIVER_OLED_Init();
  DRIVER_STEPPER_Init();
  FUNC_LEADSCREW_Init();
  osDelay(200);

  DRIVER_STEER_Rotate360(STEER_SERVO_1, STEER_DIR_CCW, 95U);
  DRIVER_STEER_Rotate360(STEER_SERVO_2, STEER_DIR_CCW, 95U);

  sSortState        = SORT_STATE_WAIT_QR;
  sCurrentTargetIdx = SORT_TARGET_INVALID;
  sStateStartTick   = 0U;
  sLeadScrewTargetCm   = 0.0f;
  sLeadScrewCmdPending = 0U;
  sStepperStopPending  = 0U;
  sProtectState        = SORT_PROTECT_NONE;
}

void user1TaskUpdata(void *argument) {
  (void)argument;
  user1TaskInit();

  for(;;) {
    user1TaskInfo.user1TaskCnt++;

    DRIVER_STEER_Updata();
    if(systemTaskInfo.systemEnable_board){
      DRIVER_STEPPER_Update();
    }

    if(user1TaskInfo.user1TaskCnt % 5U == 0U) {
      FUNC_LEADSCREW_Update();
      __updateMotorProtection();
      sortStateMachine();
    }

    if(user1TaskInfo.user1TaskCnt % 50U == 0U) {
      oledControl();
    }

    osDelay(2);
  }
}
