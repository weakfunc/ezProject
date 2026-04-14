#include "task_user1.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_GY615.h"
#include "driver_adc.h"
#include "driver_power.h"

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* 报警上限最大值（×10），对应60.0°C */
#define USER1_THRESH_HIGH_MAX    (600)
/* 报警上限最小值（×10），对应0.0°C */
#define USER1_THRESH_HIGH_MIN    (0)
/* 温度有效范围最小值（°C） */
#define USER1_TEMP_VALID_MIN     (-20.0f)
/* 温度有效范围最大值（°C） */
#define USER1_TEMP_VALID_MAX     (120.0f)
/* 显示保持10秒对应的2ms计数上限 */
#define USER1_DISPLAY_HOLD_MAX   (5000U)
/* 无操作30秒进入休眠对应的2ms计数阈值 */
#define USER1_IDLE_SLEEP_CNT     (15000U)

/*============================================================================
 * 私有变量（仅本模块使用）
 *============================================================================*/

/* 电量图标字符串，按电量由低到高排列 */
static const char * const battBarTable[] = {
  "[    ]",   /* 0~9%   */
  "[=   ]",   /* 10~24% */
  "[==  ]",   /* 25~49% */
  "[=== ]",   /* 50~74% */
  "[====]",   /* 75~100%*/
};

/*============================================================================
 * 公有变量
 *============================================================================*/

/* user1任务信息结构体实例 */
user1TaskInfo_t user1TaskInfo;

/*============================================================================
 * 私有函数声明
 *============================================================================*/

static const char *__User1Task_GetBattBar(uint8_t percent);
static void __User1Task_EvalAlarm(void);
static void __User1Task_RefreshOled(void);
static void __User1Task_HandleWakeUp(void);

/*============================================================================
 * 私有函数实现
 *============================================================================*/

/**
 * @brief  根据当前显示温度（tempDisplay）判断是否超过报警阈值
 *
 * 仅在KEY1刷新显示温度后调用，或KEY2/KEY3修改阈值后调用，
 * 不在后台周期任务中触发，避免传感器对准空气时的误报警。
 * 使用 tempDisplay（KEY1冻结值）而非 tempLatest（后台实时值）进行判断。
 */
static void __User1Task_EvalAlarm(void) {
  float highThresh;

  if(user1TaskInfo.hasValidTemp == 0U) {
    /* 尚未获得有效温度，不触发报警 */
    return;
  }

  highThresh = (float)user1TaskInfo.alarmThreshHighX10 / 10.0f;

  if(user1TaskInfo.tempDisplay > highThresh) {
    /* 显示温度超过上限阈值：蜂鸣器持续鸣叫，红灯亮、绿灯灭 */
    user1TaskInfo.alarmActive = 1U;
    boardInfo.buzzTimeMs      = 0xFFFFU;
    DRIVER_BOARD_RgbSet(BOARD_RGB_R, 1U);
    DRIVER_BOARD_RgbSet(BOARD_RGB_G, 0U);
  } else {
    /* 显示温度正常：停止报警，红灯灭、绿灯亮 */
    user1TaskInfo.alarmActive = 0U;
    boardInfo.buzzTimeMs      = 0U;
    DRIVER_BOARD_RgbSet(BOARD_RGB_R, 0U);
    DRIVER_BOARD_RgbSet(BOARD_RGB_G, 1U);
  }
}

/**
 * @brief  根据电量百分比返回电量图标字符串
 * @param  percent  电量百分比（0~100）
 * @retval 对应的图标字符串指针
 */
static const char *__User1Task_GetBattBar(uint8_t percent) {
  if(percent >= 75U) { return battBarTable[4]; }
  if(percent >= 50U) { return battBarTable[3]; }
  if(percent >= 25U) { return battBarTable[2]; }
  if(percent >= 10U) { return battBarTable[1]; }
  return battBarTable[0];
}

/**
 * @brief  刷新OLED全屏
 *
 * 显示布局（128×64像素）：
 *   y= 0（6x8） ：电量图标居中        "Bat:[====] XXX%"
 *   y=10（12x16）：目标温度大字体      "TO:XX.X C"
 *   y=28（12x16）：人体温度大字体      "BO:XX.X C"
 *   y=46（6x8） ：阈值+报警状态小字体  "Alm:XX.XC [!]" / "Alm:XX.XC [OK]"
 *   y=56（6x8） ：休眠倒计时          "Slp:XXs"
 *
 * @note   调用前需确保OLED已初始化且处于开启状态
 * @note   低电量（0~9%）时电量图标以500ms为周期闪烁
 */
static void __User1Task_RefreshOled(void) {
  uint8_t       xPos;
  uint8_t       pct = user1TaskInfo.battPercent;
  const char   *barStr;
  uint8_t       battBlink;
  float         highThreshF;
  uint32_t      remainS;

  DRIVER_OLED_Clear();

  /* ── 第1行（y=0）：电池电量居中 ─────────────────────────────
   * "Bat:[====] XXX%" 固定15字符=90px，居中起始x=19 */
  battBlink = (uint8_t)((user1TaskInfo.taskCnt / 250U) % 2U);
  if((pct < 10U) && (battBlink == 0U)) {
    barStr = "      ";  /* 低电量闪烁：熄灭图标 */
  } else {
    barStr = __User1Task_GetBattBar(pct);
  }
  DRIVER_OLED_ShowString(19U, 0U, "Bat:");
  DRIVER_OLED_ShowString(43U, 0U, barStr);
  DRIVER_OLED_ShowNum(79U, 0U, (uint32_t)pct, 3U);
  DRIVER_OLED_ShowString(97U, 0U, "%");

  /* ── 第2行（y=10）：目标温度大字体 ──────────────────────────
   * "TO:" 标签（6x8，18px），后接 12x16 温度值，再接 "C"（6x8）
   * 标签与大字起始y相同，6x8高8px仅占12x16高度的上半部分 */
  DRIVER_OLED_ShowString(0U, 10U, "TO:");
  xPos = DRIVER_OLED_ShowFloat12x16(18U, 10U, user1TaskInfo.tempDisplay, 1U);
  DRIVER_OLED_ShowChar6x8(xPos, 18U, 'C', 1U);   /* "C"纵向对齐12x16底部区域 */

  /* ── 第3行（y=28）：人体温度大字体 ──────────────────────────*/
  DRIVER_OLED_ShowString(0U, 28U, "BO:");
  xPos = DRIVER_OLED_ShowFloat12x16(18U, 28U, user1TaskInfo.bodyTempDisplay, 1U);
  DRIVER_OLED_ShowChar6x8(xPos, 36U, 'C', 1U);   /* "C"纵向对齐12x16底部区域 */

  /* ── 第4行（y=46）：报警阈值 + 报警状态小字体 ───────────────
   * "Alm:XX.XC" 后紧跟报警标志：报警中显示"[!]"，正常显示"[OK]" */
  highThreshF = (float)user1TaskInfo.alarmThreshHighX10 / 10.0f;
  DRIVER_OLED_ShowString(0U, 46U, "Alm:");
  xPos = DRIVER_OLED_ShowFloat(24U, 46U, highThreshF, 1U);
  DRIVER_OLED_ShowString(xPos, 46U, "C");
  xPos = (uint8_t)(xPos + 6U);
  if(user1TaskInfo.alarmActive != 0U) {
    DRIVER_OLED_ShowString(xPos, 46U, " [!]");
  } else {
    DRIVER_OLED_ShowString(xPos, 46U, " [OK]");
  }

  /* ── 第5行（y=56）：休眠倒计时小字体 ──────────────────────────
   * "Slp:XXs"，每500个2ms计数=1秒，满15000进入休眠 */
  if(user1TaskInfo.idleCnt < USER1_IDLE_SLEEP_CNT) {
    remainS = (USER1_IDLE_SLEEP_CNT - user1TaskInfo.idleCnt) / 500U;
  } else {
    remainS = 0U;
  }
  DRIVER_OLED_ShowString(0U, 56U, "Slp:");
  DRIVER_OLED_ShowNum(24U, 56U, remainS, 2U);
  DRIVER_OLED_ShowString(36U, 56U, "s");

  DRIVER_OLED_Refresh();
}

/**
 * @brief  从休眠状态唤醒：调用driver_power唤醒接口，恢复OLED显示，清除休眠标志
 */
static void __User1Task_HandleWakeUp(void) {
  DRIVER_POWER_WakeUp();
  user1TaskInfo.sleepFlag = 0U;
  user1TaskInfo.idleCnt   = 0U;
  /* 恢复绿灯（报警状态由 EvalAlarm 在按键处理后重新设置） */
  DRIVER_BOARD_RgbSet(BOARD_RGB_G, 1U);
  DRIVER_OLED_DisplayOn();
  __User1Task_RefreshOled();
}

/*============================================================================
 * API接口实现
 *============================================================================*/

/**
 * @brief  初始化user1任务，依次初始化各driver并设置应用初始状态
 */
void user1TaskInit(void) {
  /* 初始化红外温度传感器驱动（GY615，UART3接口） */
  DRIVER_GY615_Init();

  /* 等待OLED上电稳定：SSD1306数据手册要求VCC稳定后约100ms才可接受I2C命令。
   * 复位时OLED已上电故无症状，冷启动时MCU先于OLED就绪，不加延时则初始化命令被忽略。 */
  osDelay(100U);
  /* 初始化OLED驱动（SSD1306，I2C接口） */
  DRIVER_OLED_Init();

  /* 初始化ADC驱动模块（读取stdlib已采样的电池电压） */
  DRIVER_ADC_Init();

  /* 初始化应用信息结构体 */
  user1TaskInfo.taskCnt            = 0U;
  user1TaskInfo.tempLatest         = 0.0f;
  user1TaskInfo.tempDisplay        = 0.0f;
  user1TaskInfo.bodyTempLatest     = 0.0f;
  user1TaskInfo.bodyTempDisplay    = 0.0f;
  user1TaskInfo.alarmThreshHighX10 = 375;    /* 初始上限37.5°C，超过则报警 */
  /* 边沿检测缓存与当前按下计数同步，避免初始化后立即触发误事件 */
  user1TaskInfo.lastKey1PressCount = boardInfo.key[BOARD_KEY1].pressCount;
  user1TaskInfo.lastKey2PressCount = boardInfo.key[BOARD_KEY2].pressCount;
  user1TaskInfo.lastKey3PressCount = boardInfo.key[BOARD_KEY3].pressCount;
  user1TaskInfo.key1Pressed        = 0U;
  user1TaskInfo.key2Pressed        = 0U;
  user1TaskInfo.key3Pressed        = 0U;
  user1TaskInfo.alarmActive        = 0U;
  user1TaskInfo.battPercent        = 0U;
  user1TaskInfo.displayHoldCnt     = 0U;
  user1TaskInfo.idleCnt            = 0U;
  user1TaskInfo.sleepFlag          = 0U;
  user1TaskInfo.hasValidTemp       = 0U;

  /* 上电首次温度采集：发送请求后等待50ms（GY615响应约10ms），
   * 读取初始温度直接显示在OLED，避免开机显示0.0°C */
  {
    GY615Info_t snapshot;
    DRIVER_GY615_Request();
    osDelay(50U);
    if(DRIVER_GY615_GetInfo(&snapshot) != 0U) {
      if((snapshot.targetTempC >= USER1_TEMP_VALID_MIN) &&
         (snapshot.targetTempC <= USER1_TEMP_VALID_MAX)) {
        user1TaskInfo.tempLatest      = snapshot.targetTempC;
        user1TaskInfo.tempDisplay     = snapshot.targetTempC;
        user1TaskInfo.bodyTempLatest  = snapshot.bodyTempC;
        user1TaskInfo.bodyTempDisplay = snapshot.bodyTempC;
        user1TaskInfo.hasValidTemp    = 1U;
      }
    }
    /* 无论是否取到有效值，继续发送一次请求供主循环使用 */
    DRIVER_GY615_Request();
  }

  /* 初始化显示（显示首次温度或初始状态） */
  __User1Task_RefreshOled();
}

/**
 * @brief  user1任务主循环入口（FreeRTOS任务函数，基础周期2ms）
 * @param  argument  FreeRTOS任务参数（未使用）
 *
 * 子周期划分：
 *   taskCnt % 50  == 0  →  100ms：后台温度采集（更新tempLatest，不触发报警）
 *   taskCnt % 250 == 0  →  500ms：ADC电量更新、OLED电量刷新
 *   KEY1按下时           →  刷新显示温度并评估报警状态
 */
void user1TaskUpdata(void *argument) {
  user1TaskInit();

  for(;;) {
    user1TaskInfo.taskCnt++;

    /* ================================================================
     * ① 每2ms（每轮）：按键边沿检测
     *    通过比较boardInfo.key.pressCount（由system任务10ms更新）
     *    检测新按下事件，置对应标志位
     * ================================================================*/
    {
      if(boardInfo.key[BOARD_KEY1].pressCount != user1TaskInfo.lastKey1PressCount) {
        user1TaskInfo.lastKey1PressCount = boardInfo.key[BOARD_KEY1].pressCount;
        user1TaskInfo.key1Pressed = 1U;
      }
      if(boardInfo.key[BOARD_KEY2].pressCount != user1TaskInfo.lastKey2PressCount) {
        user1TaskInfo.lastKey2PressCount = boardInfo.key[BOARD_KEY2].pressCount;
        user1TaskInfo.key2Pressed = 1U;
      }
      if(boardInfo.key[BOARD_KEY3].pressCount != user1TaskInfo.lastKey3PressCount) {
        user1TaskInfo.lastKey3PressCount = boardInfo.key[BOARD_KEY3].pressCount;
        user1TaskInfo.key3Pressed = 1U;
      }
    }

    /* ================================================================
     * ② 每2ms（每轮）：按键事件处理
     * ================================================================*/
    {
      /* KEY1：将后台最新温度（目标温度+人体温度）更新到显示值，重置显示保持计时，
       * 刷新显示温度后评估报警状态，再刷新OLED */
      if(user1TaskInfo.key1Pressed != 0U) {
        user1TaskInfo.key1Pressed     = 0U;
        user1TaskInfo.tempDisplay     = user1TaskInfo.tempLatest;
        user1TaskInfo.bodyTempDisplay = user1TaskInfo.bodyTempLatest;
        user1TaskInfo.displayHoldCnt  = 0U;
        user1TaskInfo.idleCnt         = 0U;
        __User1Task_EvalAlarm();       /* 基于新显示温度判断报警 */
        if(user1TaskInfo.sleepFlag != 0U) {
          __User1Task_HandleWakeUp();
        } else {
          __User1Task_RefreshOled();
        }
      }

      /* KEY2：报警上限+0.1°C，限幅到60.0°C，重新评估报警状态，刷新OLED */
      if(user1TaskInfo.key2Pressed != 0U) {
        user1TaskInfo.key2Pressed = 0U;
        user1TaskInfo.alarmThreshHighX10++;
        if(user1TaskInfo.alarmThreshHighX10 > USER1_THRESH_HIGH_MAX) {
          user1TaskInfo.alarmThreshHighX10 = (int16_t)USER1_THRESH_HIGH_MAX;
        }
        user1TaskInfo.idleCnt = 0U;
        __User1Task_EvalAlarm();       /* 阈值变化后重新评估当前显示温度 */
        if(user1TaskInfo.sleepFlag != 0U) {
          __User1Task_HandleWakeUp();
        } else {
          __User1Task_RefreshOled();
        }
      }

      /* KEY3：报警上限-0.1°C，限幅到0.0°C，重新评估报警状态，刷新OLED */
      if(user1TaskInfo.key3Pressed != 0U) {
        user1TaskInfo.key3Pressed = 0U;
        user1TaskInfo.alarmThreshHighX10--;
        if(user1TaskInfo.alarmThreshHighX10 < USER1_THRESH_HIGH_MIN) {
          user1TaskInfo.alarmThreshHighX10 = (int16_t)USER1_THRESH_HIGH_MIN;
        }
        user1TaskInfo.idleCnt = 0U;
        __User1Task_EvalAlarm();       /* 阈值变化后重新评估当前显示温度 */
        if(user1TaskInfo.sleepFlag != 0U) {
          __User1Task_HandleWakeUp();
        } else {
          __User1Task_RefreshOled();
        }
      }
    }

    /* ================================================================
     * ③ 每100ms：后台温度采集（仅更新tempLatest/bodyTempLatest）
     *    报警判断仅在KEY1刷新显示温度时触发，不在此处判断，
     *    避免传感器对准空气时误报。
     * ================================================================*/
    if(user1TaskInfo.taskCnt % 50U == 0U) {
      GY615Info_t snapshot;

      /* 发送温度查询请求（约10ms后由UART中断回调接收） */
      DRIVER_GY615_Request();

      /* 读取上次请求的温度数据（管道化：本次读上次请求的结果） */
      if(DRIVER_GY615_GetInfo(&snapshot) != 0U) {
        /* 有效性校验：targetTempC超出-20°C~120°C范围视为无效，保持上次有效值 */
        if((snapshot.targetTempC >= USER1_TEMP_VALID_MIN) &&
           (snapshot.targetTempC <= USER1_TEMP_VALID_MAX)) {
          user1TaskInfo.tempLatest     = snapshot.targetTempC;
          user1TaskInfo.bodyTempLatest = snapshot.bodyTempC;
          user1TaskInfo.hasValidTemp   = 1U;
        }
      }
    }

    /* ================================================================
     * ④ 每500ms：ADC电量更新、OLED电量区域刷新
     * ================================================================*/
    if(user1TaskInfo.taskCnt % 250U == 0U) {
      /* 从driver_adc读取已由system任务采样的电池电压换算结果 */
      DRIVER_ADC_UpdateBattPercent();
      user1TaskInfo.battPercent = adcDriverInfo.battPercent;
      /* 未休眠时刷新电量显示 */
      if(user1TaskInfo.sleepFlag == 0U) {
        __User1Task_RefreshOled();
      }
    }

    /* ================================================================
     * ⑤ 每2ms（每轮）：显示保持计时（最大计到5000=10秒）
     * ================================================================*/
    if(user1TaskInfo.displayHoldCnt < USER1_DISPLAY_HOLD_MAX) {
      user1TaskInfo.displayHoldCnt++;
    }

    /* ================================================================
     * ⑥ 每2ms（每轮）：休眠计时与低功耗进入
     * ================================================================*/
    if(user1TaskInfo.sleepFlag == 0U) {
      user1TaskInfo.idleCnt++;

      if(user1TaskInfo.idleCnt >= USER1_IDLE_SLEEP_CNT) {
        /* 进入休眠：关闭蜂鸣器、红灯、绿灯、OLED，然后进入低功耗模式 */
        boardInfo.buzzTimeMs = 0U;
        DRIVER_BOARD_RgbSet(BOARD_RGB_R, 0U);
        DRIVER_BOARD_RgbSet(BOARD_RGB_G, 0U);
        DRIVER_OLED_Clear();
        DRIVER_OLED_Refresh();
        DRIVER_OLED_DisplayOff();
        user1TaskInfo.sleepFlag = 1U;
      }
    } else {
      /* 休眠中：每轮进入WFI低功耗等待，由中断唤醒后继续检测按键 */
      DRIVER_POWER_EnterSleep();
    }

    osDelay(2);
  }
}
