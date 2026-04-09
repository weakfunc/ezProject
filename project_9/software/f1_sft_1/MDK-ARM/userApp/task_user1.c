#include "task_user1.h"
#include "driver_ble.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_senser.h"
#include "func_appcom.h"
#include <stdio.h>

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* 标定采样次数（每50ms一次，共约500ms完成标定） */
#define CALIB_SAMPLE_COUNT   (10U)
/* 标定超时次数（每50ms加1，共约2000ms；超时后使用默认地面距离） */
#define CALIB_TIMEOUT_COUNT  (40U)
/* 标定超时时使用的默认地面距离（mm） */
#define CALIB_DEFAULT_GROUND_MM  (500.0f)
/* 液位有效范围下限（mm） */
#define LEVEL_VALID_MIN_MM  (0.0f)
/* 液位有效范围上限（mm） */
#define LEVEL_VALID_MAX_MM  (5000.0f)
/* 滑动均值滤波窗口大小 */
#define FILTER_WIN_SIZE     (5U)

/*============================================================================
 * 公有变量
 *============================================================================*/

/* 液位测量系统任务信息（公有，供 func_appcom 等模块访问） */
user1TaskInfo_t user1TaskInfo;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 滑动均值滤波：将新值写入循环缓冲区，返回当前窗口均值。
 * newVal : 本次有效液位高度（mm）
 * 返回值 : 滤波后的液位均值（mm）
 */
static float __user1_Filter(float newVal)
{
  float   sum;
  uint8_t count;
  uint8_t i;

  /* 写入新值，推进写入索引 */
  user1TaskInfo.filterBuf[user1TaskInfo.filterIdx] = newVal;
  user1TaskInfo.filterIdx = (uint8_t)((user1TaskInfo.filterIdx + 1U) % FILTER_WIN_SIZE);
  if (user1TaskInfo.filterIdx == 0U) {
    user1TaskInfo.filterFull = 1U;  /* 索引回绕，缓冲区已填满 */
  }

  /* 计算有效样本均值 */
  count = user1TaskInfo.filterFull ? FILTER_WIN_SIZE : user1TaskInfo.filterIdx;
  sum = 0.0f;
  for (i = 0U; i < count; i++) {
    sum += user1TaskInfo.filterBuf[i];
  }
  return sum / (float)count;
}

/* 刷新 OLED 显示内容：
 *   校准中（calibDone==0）：显示系统名称和校准提示；
 *   正常工作（calibDone==1）：显示液位、变化量、距液面距离、高报警阈值。
 * 状态切换时先清屏避免残留字符，同一状态内直接覆盖写入避免闪烁。
 */
static void __user1_OledRefresh(void)
{
  /* 上次刷新时的标定状态，初始化为无效值以保证首次必清屏 */
  static uint8_t prevCalibDone = 0xFFU;
  char buf[32];

  /* 检测状态切换，切换时先清屏 */
  if (prevCalibDone != user1TaskInfo.calibDone) {
    DRIVER_OLED_Clear();
    prevCalibDone = user1TaskInfo.calibDone;
  }

  if (user1TaskInfo.calibDone == 0U) {
    /* ---- 校准中状态显示 ---- */
    DRIVER_OLED_ShowString(0,  0, "LiqLevel System ");
    DRIVER_OLED_ShowString(0,  8, "Calibrating...  ");
    DRIVER_OLED_ShowString(0, 16, "Please wait...  ");
    DRIVER_OLED_ShowString(0, 24, "                ");
  } else {
    /* ---- 正常工作状态显示 ---- */

    /* 第0行：液位高度 */
    sprintf(buf, "Lvl:%5d mm  ", (int)user1TaskInfo.liquidLevel_mm);
    DRIVER_OLED_ShowString(0, 0, buf);

    /* 第1行：液位变化量（带正负号） */
    sprintf(buf, "Dlt:%+5d mm  ", (int)user1TaskInfo.liquidLevelDelta_mm);
    DRIVER_OLED_ShowString(0, 8, buf);

    /* 第2行：模块距液面原始距离 */
    sprintf(buf, "Dst:%5d mm  ", (int)user1TaskInfo.distToLiquid_mm);
    DRIVER_OLED_ShowString(0, 16, buf);

    /* 第3行：高报警阈值（未设置时显示 ----） */
    if (user1TaskInfo.alarmHigh_mm > 0.0f) {
      sprintf(buf, "Alm:%5d mm  ", (int)user1TaskInfo.alarmHigh_mm);
    } else {
      sprintf(buf, "Alm:  ----      ");
    }
    DRIVER_OLED_ShowString(0, 24, buf);
  }

  DRIVER_OLED_Refresh();
}

/*============================================================================
 * 任务入口
 *============================================================================*/

/* 初始化所有驱动和任务内部状态 */
void user1TaskInit(void)
{
  uint8_t i;

  /* 初始化所需驱动 */
  DRIVER_BLE_Init();
  DRIVER_SENSER_Init();
  DRIVER_OLED_Init();

  /* 初始化内部状态 */
  user1TaskInfo.taskCnt              = 0U;
  user1TaskInfo.calibDone            = 0U;
  user1TaskInfo.calibCnt             = 0U;
  user1TaskInfo.calibTimeoutCnt      = 0U;
  user1TaskInfo.calibSum             = 0.0f;
  user1TaskInfo.distToGround_mm      = 0.0f;
  user1TaskInfo.distToLiquid_mm      = 0.0f;
  user1TaskInfo.liquidLevel_mm       = 0.0f;
  user1TaskInfo.liquidLevelInit_mm   = 0.0f;
  user1TaskInfo.liquidLevelDelta_mm  = 0.0f;
  user1TaskInfo.alarmHigh_mm         = 200.0f;  /* 默认高报警阈值200mm */
  user1TaskInfo.alarmLow_mm          = 0.0f;
  user1TaskInfo.filterIdx            = 0U;
  user1TaskInfo.filterFull           = 0U;
  for (i = 0U; i < FILTER_WIN_SIZE; i++) {
    user1TaskInfo.filterBuf[i] = 0.0f;
  }
}

/* 液位测量系统主任务（2ms基础周期）
 * 调度表：
 *   50ms  （%25==0）：测距采样、标定/液位计算、读取APP报警阈值
 *   100ms （%50==0）：上报数据至 remoteVar_TX
 *   200ms （%100==0）：刷新 OLED 显示
 */
void user1TaskUpdata(void *argument)
{
  uint16_t rawDist;   /* 超声波测距原始值（mm） */
  float    rawLevel;  /* 原始液位高度（mm），未滤波 */
  float    rxHigh;    /* APP 下发的高报警阈值 */
  float    rxLow;     /* APP 下发的低报警阈值 */

  user1TaskInit();
  for (;;) {
    user1TaskInfo.taskCnt++;

    /* ---- 50ms：测距与计算 ---- */
    if (user1TaskInfo.taskCnt % 25U == 0U) {

      if (user1TaskInfo.calibDone == 0U) {
        /* 功能模块1：上电标定 —— 采集普通超声波地面距离均值 */
        user1TaskInfo.calibTimeoutCnt++;

        if (DRIVER_SENSER_GetHCSR04Distance(&rawDist) == 1U) {
          if (rawDist > 0U) {
            user1TaskInfo.calibSum += (float)rawDist;
            user1TaskInfo.calibCnt++;
          }
        }

        /* 采集满10次正常完成，或超时则使用默认地面距离 */
        if ((user1TaskInfo.calibCnt >= CALIB_SAMPLE_COUNT) ||
            (user1TaskInfo.calibTimeoutCnt >= CALIB_TIMEOUT_COUNT)) {

          if (user1TaskInfo.calibCnt >= CALIB_SAMPLE_COUNT) {
            /* 正常完成：使用实测均值 */
            user1TaskInfo.distToGround_mm = user1TaskInfo.calibSum / (float)CALIB_SAMPLE_COUNT;
          } else {
            /* 超时：使用默认地面距离 500mm */
            user1TaskInfo.distToGround_mm = CALIB_DEFAULT_GROUND_MM;
          }

          /* 读取一次高精度超声波，记录初始液位高度 */
          if (DRIVER_SENSER_GetDistance(&rawDist) == 1U) {
            user1TaskInfo.distToLiquid_mm     = (float)rawDist;
            user1TaskInfo.liquidLevelInit_mm  = user1TaskInfo.distToGround_mm - user1TaskInfo.distToLiquid_mm;
            user1TaskInfo.liquidLevel_mm      = user1TaskInfo.liquidLevelInit_mm;
            user1TaskInfo.liquidLevelDelta_mm = 0.0f;
          }
          user1TaskInfo.calibDone = 1U;
        }

      } else {
        /* 功能模块2：液位高度实时测量与滤波 */
        if (DRIVER_SENSER_GetDistance(&rawDist) == 1U) {
          rawLevel = user1TaskInfo.distToGround_mm - (float)rawDist;

          /* 有效性校验：超出量程范围则丢弃本次数据 */
          if ((rawLevel >= LEVEL_VALID_MIN_MM) && (rawLevel <= LEVEL_VALID_MAX_MM)) {
            user1TaskInfo.liquidLevel_mm      = __user1_Filter(rawLevel);
            user1TaskInfo.distToLiquid_mm     = (float)rawDist;
            user1TaskInfo.liquidLevelDelta_mm = user1TaskInfo.liquidLevel_mm - user1TaskInfo.liquidLevelInit_mm;
          }
        }

        /* 功能模块3：读取 APP 下发报警阈值（>0 时认为用户有设置意图） */
        rxHigh = remoteInfo.remoteVar_RX[0].var_float;
        rxLow  = remoteInfo.remoteVar_RX[1].var_float;
        if (rxHigh > 0.0f) {
          user1TaskInfo.alarmHigh_mm = rxHigh;
        }
        if (rxLow > 0.0f) {
          user1TaskInfo.alarmLow_mm = rxLow;
        }
      }
    }

    /* ---- 100ms：上报数据至 APP ---- */
    if (user1TaskInfo.taskCnt % 50U == 0U) {
      /* 功能模块4：仅写本项目使用的索引，未使用的索引不操作 */
      remoteInfo.remoteVar_TX[4].var_float  = user1TaskInfo.liquidLevel_mm;
      remoteInfo.remoteVar_TX[5].var_float  = user1TaskInfo.liquidLevelDelta_mm;
      remoteInfo.remoteVar_TX[6].var_uint32 = (uint32_t)user1TaskInfo.calibDone;
      /* [3] 保留，不操作 */
      remoteInfo.remoteVar_TX[8].var_float  = user1TaskInfo.distToLiquid_mm;
      remoteInfo.remoteVar_TX[9].var_float  = user1TaskInfo.alarmHigh_mm;
      /* [6][7] 保留，不操作 */
    }

    /* ---- 200ms：刷新 OLED ---- */
    if (user1TaskInfo.taskCnt % 100U == 0U) {
      /* 功能模块5：OLED 显示刷新 */
      __user1_OledRefresh();
    }

    /* ---- 每周期：报警硬件输出 ---- */
    if ((user1TaskInfo.calibDone == 1U) &&
        (user1TaskInfo.alarmHigh_mm > 0.0f) &&
        (user1TaskInfo.liquidLevel_mm >= user1TaskInfo.alarmHigh_mm)) {
      /* 液位超阈值：点亮红灯 */
      DRIVER_BOARD_RgbOn(BOARD_RGB_R);
      /* 每1000ms触发一次200ms蜂鸣（仅在蜂鸣器空闲时写入，避免重复触发） */
      if ((user1TaskInfo.taskCnt % 500U == 0U) && (boardInfo.buzzTimeMs == 0U)) {
        boardInfo.buzzTimeMs = 200U;
      }
    } else {
      /* 液位正常或标定中：关闭红灯，停止蜂鸣 */
      DRIVER_BOARD_RgbOff(BOARD_RGB_R);
      boardInfo.buzzTimeMs = 0U;
    }

    osDelay(2);
  }
}
