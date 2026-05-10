#include "task_user1.h"
#include "func_appcom.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_steer.h"
#include "driver_senser.h"
#include "stdlib_common.h"
#include "stdlib_flash.h"
#include "task_system.h"
#include <string.h>

/*============================================================================
 * README
 *
 * 本文件实现宠物智能饲喂系统的应用主任务。
 * 任务基础周期为 2ms，通过 taskCnt 分频出 10ms/50ms/100ms/500ms/1s 子任务：
 *   10ms  ：使用最近一次缓存重量推进投喂闭环、卡堵检测
 *   50ms  ：红外/RFID 慢速传感器预留入口
 *   100ms ：称重采样、APP 命令处理、报警状态评估、蜂鸣器请求
 *   500ms ：计划触发检查、状态变量写入 TX、OLED 状态页刷新
 *   1s    ：系统秒计数、计划列表轮播、Flash dirty 维护
 *
 * 重要约束：
 *   1. task 层不直接调用 HAL；
 *   2. task 层不做字节序处理；
 *   3. 发送给 APP 的数据只写 remoteVar_TX[4]~[15]；
 *   4. 当前工程无红外/RFID driver，相关字段保持 0 并保留接入位置。
 *============================================================================*/

/*============================================================================
 * 应用参数
 *============================================================================*/
#define USER1_DEFAULT_ALARM_THRESHOLD_G  (20.0f)
#define USER1_TARGET_CLOSE_ADVANCE_G     (35.0f)
#define USER1_JAM_TIMEOUT_MS             (10000U)
#define USER1_JAM_MIN_DELTA_G            (1.0f)
#define USER1_SERVO_MOVE_TIME_MS         (500U)
#define USER1_MANUAL_TARGET_WEIGHT_G     (1000.0f)
#define USER1_BLE_HEARTBEAT_TIMEOUT_MS   (30000U)
#define USER1_PLAN_FREQ_MAX_SEC          (86400.0f)
#define USER1_PLAN_WEIGHT_MAX_G          (1000.0f)
#define USER1_FEED_SERVO_ID              STEER_SERVO_1
#define USER1_BEEP_PERIOD_TICKS          (250U)
#define USER1_BEEP_TIME_MS               (100U)
#define USER1_BEEP_CONTINUOUS_TIME_MS    (0xFFFFU)
#define USER1_DEFAULT_PLAN1_FREQ_SEC     (60.0f)
#define USER1_DEFAULT_PLAN1_WEIGHT_G     (100.0f)
#define USER1_KEY_TEST_FEED_WEIGHT_G     (50.0f)
#define USER1_KEY_ENABLE_INDEX           BOARD_KEY1
#define USER1_KEY_PLAN_SWITCH_INDEX      BOARD_KEY2
#define USER1_KEY_TEST_FEED_INDEX        BOARD_KEY3
#define USER1_WAIT_SYSTEM_INIT_MS        (1000U)

#if USER1_PLAN_MAX_COUNT != USER_FLASH_PLAN_MAX_COUNT
#error "USER1_PLAN_MAX_COUNT must match USER_FLASH_PLAN_MAX_COUNT"
#endif

/*============================================================================
 * 公有变量
 *============================================================================*/
user1TaskInfo_t user1TaskInfo;

/*============================================================================
 * 私有变量
 *============================================================================*/
static uint8_t  user1ManualFeeding;
static uint8_t  user1PlansDirty;
static uint8_t  user1HistoryDirty;
static uint8_t  user1ConfigDirty;
static uint8_t  user1FlashStoreReady;
static uint32_t user1LastBleTickMs;
static uint32_t user1LastEnableKeyCount;
static uint32_t user1LastPlanSwitchKeyCount;
static uint32_t user1LastTestFeedKeyCount;

/*============================================================================
 * 私有函数
 *============================================================================*/
/* 判断方案 ID 是否落在需求定义的 1~5 范围内。 */
static uint8_t __USER1_IsValidPlanId(uint8_t planId){
    return ((planId >= 1U) && (planId <= USER1_PLAN_MAX_COUNT)) ? 1U : 0U;
}

/* 校验计划参数，避免 APP 下发 0、负数或明显越界值导致状态机异常。 */
static uint8_t __USER1_IsValidPlanParam(float freqSec, float weightG){
    if(freqSec <= 0.0f || freqSec > USER1_PLAN_FREQ_MAX_SEC) return 0U;
    if(weightG <= 0.0f || weightG > USER1_PLAN_WEIGHT_MAX_G) return 0U;
    return 1U;
}

/* 按 planId 查找已存在的投喂方案；找不到返回 NULL。 */
static user1Plan_t *__USER1_FindPlan(uint8_t planId){
    uint8_t i;

    if(__USER1_IsValidPlanId(planId) == 0U) return NULL;

    for(i = 0U; i < USER1_PLAN_MAX_COUNT; i++){
        if(user1TaskInfo.plans[i].planId == planId){
            return &user1TaskInfo.plans[i];
        }
    }
    return NULL;
}

/* 查找空方案槽位，planId=0 表示该槽位当前未使用。 */
static user1Plan_t *__USER1_FindEmptyPlanSlot(void){
    uint8_t i;

    for(i = 0U; i < USER1_PLAN_MAX_COUNT; i++){
        if(user1TaskInfo.plans[i].planId == 0U){
            return &user1TaskInfo.plans[i];
        }
    }
    return NULL;
}

/* 初始化测试用例要求的默认方案 1：每 60s 投喂一次，每次目标 100g。
 * lastFeedTickMs 取当前 tick，使系统使能后从完整倒计时开始执行。 */
static void __USER1_InitDefaultPlan1(void){
    user1Plan_t *plan = __USER1_FindPlan(1U);

    if(plan == NULL){
        plan = __USER1_FindEmptyPlanSlot();
    }
    if(plan == NULL){
        plan = &user1TaskInfo.plans[0];
    }

    plan->planId = 1U;
    plan->reserved[0] = 0U;
    plan->reserved[1] = 0U;
    plan->reserved[2] = 0U;
    plan->frequencySec = USER1_DEFAULT_PLAN1_FREQ_SEC;
    plan->weightG = USER1_DEFAULT_PLAN1_WEIGHT_G;
    plan->lastFeedTickMs = STDLIB_COMMON_GetTickMs();
}

/* 当前 Flash 结构只有 version 字段；在不改底层的前提下，暂用它保存“上一次方案 ID”。 */
/* 将 RAM 中的投喂计划表同步到 flashStore 缓存；真正擦写由 systemTask 调 STDLIB_FLASH_Save()。 */
static void __USER1_SyncPlansToFlashCache(void){
    uint8_t i;

    for(i = 0U; i < USER1_PLAN_MAX_COUNT; i++){
        flashStore.plans[i].planId = user1TaskInfo.plans[i].planId;
        flashStore.plans[i].reserved[0] = 0U;
        flashStore.plans[i].reserved[1] = 0U;
        flashStore.plans[i].reserved[2] = 0U;
        flashStore.plans[i].frequencySec = user1TaskInfo.plans[i].frequencySec;
        flashStore.plans[i].weightG = user1TaskInfo.plans[i].weightG;
        flashStore.plans[i].lastFeedTickMs = user1TaskInfo.plans[i].lastFeedTickMs;
    }
}

/* 从 Flash 恢复投喂计划表；运行 tick 不从 Flash 读取，上电后从当前 tick 重新计时。 */
static uint8_t __USER1_LoadPlansFromFlash(void){
    uint8_t i;
    uint8_t loaded = 0U;
    uint32_t nowTickMs = STDLIB_COMMON_GetTickMs();

    if(STDLIB_FLASH_IsValid() == 0U) return 0U;

    memset(user1TaskInfo.plans, 0, sizeof(user1TaskInfo.plans));
    for(i = 0U; i < USER1_PLAN_MAX_COUNT; i++){
        if(flashStore.plans[i].planId == 0U) continue;
        if(__USER1_IsValidPlanId(flashStore.plans[i].planId) == 0U) continue;
        if(__USER1_IsValidPlanParam(flashStore.plans[i].frequencySec,
                                    flashStore.plans[i].weightG) == 0U){
            continue;
        }

        user1TaskInfo.plans[i].planId = flashStore.plans[i].planId;
        user1TaskInfo.plans[i].reserved[0] = 0U;
        user1TaskInfo.plans[i].reserved[1] = 0U;
        user1TaskInfo.plans[i].reserved[2] = 0U;
        user1TaskInfo.plans[i].frequencySec = flashStore.plans[i].frequencySec;
        user1TaskInfo.plans[i].weightG = flashStore.plans[i].weightG;
        /* Flash 中保存了完整字段，但重启后系统 tick 从 0 开始，运行计时需重新校准。 */
        user1TaskInfo.plans[i].lastFeedTickMs = nowTickMs;
        loaded = 1U;
    }

    return loaded;
}

static uint8_t __USER1_LoadLastPlanIdFromFlash(void){
    uint32_t storedPlanId = flashStore.lastPlanId;

    if((STDLIB_FLASH_IsValid() != 0U) &&
       (storedPlanId >= 1U) &&
       (storedPlanId <= USER1_PLAN_MAX_COUNT) &&
       (__USER1_FindPlan((uint8_t)storedPlanId) != NULL)){
        return (uint8_t)storedPlanId;
    }

    return 1U;
}

/* 标记当前方案需要持久化；真正写 Flash 仍由系统任务里的 STDLIB_FLASH_Save() 周期完成。 */
static void __USER1_MarkCurrentPlanForFlash(void){
    if(__USER1_IsValidPlanId(user1TaskInfo.currentPlanId) != 0U){
        flashStore.lastPlanId = (uint32_t)user1TaskInfo.currentPlanId;
        user1ConfigDirty = 1U;
    }
}

/* 按 pressCount 边沿识别一次短按事件，避免长按期间重复触发测试逻辑。 */
static uint8_t __USER1_IsKeyPressEvent(uint8_t keyIndex, uint32_t *lastPressCount){
    uint32_t nowPressCount;

    if(keyIndex >= BOARD_KEY_COUNT) return 0U;

    nowPressCount = boardInfo.key[keyIndex].pressCount;
    if(nowPressCount != *lastPressCount){
        *lastPressCount = nowPressCount;
        return 1U;
    }

    return 0U;
}

/* 取得当前方案；若当前 ID 无效，则自动回到方案 1，保证 OLED 和计划触发都有兜底值。 */
static user1Plan_t *__USER1_GetCurrentPlanOrDefault(void){
    user1Plan_t *plan = __USER1_FindPlan(user1TaskInfo.currentPlanId);

    if(plan == NULL){
        plan = __USER1_FindPlan(1U);
        if(plan != NULL){
            user1TaskInfo.currentPlanId = 1U;
        }
    }

    return plan;
}

/* 系统使能时重置当前方案计时点，让“每 60s 一次”从按键启动后开始计时。 */
static void __USER1_ResetCurrentPlanTimer(void){
    user1Plan_t *plan = __USER1_GetCurrentPlanOrDefault();

    if(plan != NULL){
        plan->lastFeedTickMs = STDLIB_COMMON_GetTickMs();
        user1PlansDirty = 1U;
    }
}

/* key[1] 切换到下一个已配置方案；当前只有方案 1 时会保持在方案 1。 */
static uint8_t __USER1_FindNextPlanId(void){
    uint8_t offset;
    uint8_t candidate;
    uint8_t startPlanId = user1TaskInfo.currentPlanId;

    if(__USER1_IsValidPlanId(startPlanId) == 0U){
        startPlanId = 1U;
    }

    for(offset = 1U; offset <= USER1_PLAN_MAX_COUNT; offset++){
        candidate = (uint8_t)(startPlanId + offset);
        while(candidate > USER1_PLAN_MAX_COUNT){
            candidate = (uint8_t)(candidate - USER1_PLAN_MAX_COUNT);
        }

        if(__USER1_FindPlan(candidate) != NULL){
            return candidate;
        }
    }

    return 1U;
}

/* 切换当前方案并记录到 Flash 缓存，掉电后优先恢复这个方案 ID。 */
static void __USER1_SelectPlan(uint8_t planId){
    if(__USER1_FindPlan(planId) == NULL) return;

    user1TaskInfo.currentPlanId = planId;
    __USER1_MarkCurrentPlanForFlash();
}

/* 计算距当前方案下一次自动投喂的倒计时，单位 s；已到期时返回 0。 */
static uint32_t __USER1_GetNextFeedCountdownSec(void){
    user1Plan_t *plan = __USER1_GetCurrentPlanOrDefault();
    uint32_t nowTickMs;
    uint32_t freqMs;
    uint32_t elapsedMs;
    uint32_t remainMs;

    if(plan == NULL) return 0U;
    if(__USER1_IsValidPlanParam(plan->frequencySec, plan->weightG) == 0U) return 0U;

    freqMs = (uint32_t)(plan->frequencySec * 1000.0f);
    if(freqMs == 0U) return 0U;
    if(user1TaskInfo.planEnabled == 0U){
        return (freqMs + 999U) / 1000U;
    }

    nowTickMs = STDLIB_COMMON_GetTickMs();
    elapsedMs = nowTickMs - plan->lastFeedTickMs;
    if(elapsedMs >= freqMs) return 0U;

    remainMs = freqMs - elapsedMs;
    return (remainMs + 999U) / 1000U;
}

/* 360 度舵机停止，对连续旋转舵机使用 1500us 中位脉宽。 */
static void __USER1_ServoStop(void){
    DRIVER_STEER_Rotate180(USER1_FEED_SERVO_ID, STEER_360_STOP);
}

/* 开槽动作：按约定顺时针旋转一小段时间后停止。 */
static void __USER1_ServoOpen(void){
    DRIVER_STEER_Rotate360(USER1_FEED_SERVO_ID,
                           STEER_DIR_CW,
                           500);
}

/* 关槽动作：按约定逆时针旋转一小段时间后停止。 */
static void __USER1_ServoClose(void){
    DRIVER_STEER_Rotate360(USER1_FEED_SERVO_ID,
                           STEER_DIR_CCW,
                           530);
}

/* 记录一次投喂历史，使用环形缓冲保存最近 USER1_HISTORY_MAX_COUNT 条。 */
static void __USER1_AddHistory(uint8_t planId, float actualWeightG){
    user1FeedRecord_t *record;

    if(actualWeightG < 0.0f){
        actualWeightG = 0.0f;
    }

    record = &user1TaskInfo.history[user1TaskInfo.historyHead];
    record->planId = planId;
    record->reserved[0] = 0U;
    record->reserved[1] = 0U;
    record->reserved[2] = 0U;
    record->actualWeightG = actualWeightG;
    record->timestampSec = user1TaskInfo.systemSec;

    user1TaskInfo.historyHead++;
    if(user1TaskInfo.historyHead >= USER1_HISTORY_MAX_COUNT){
        user1TaskInfo.historyHead = 0U;
    }
    if(user1TaskInfo.historyCount < USER1_HISTORY_MAX_COUNT){
        user1TaskInfo.historyCount++;
    }

    user1HistoryDirty = 1U;
}

/* 启动一次投喂。
 * planId      : 自动模式下为计划 ID，手动模式下记录当前计划 ID；
 * weightG     : 本次希望增加的重量；
 * manualMode  : 1=APP 手动投喂，0=计划自动投喂。
 */
static void __USER1_StartFeed(uint8_t planId, float weightG, uint8_t manualMode){
    if(user1TaskInfo.feedingState != USER1_FEED_STATE_IDLE) return;
    if(weightG <= 0.0f) return;

    user1ManualFeeding = manualMode;
    user1TaskInfo.currentPlanId = planId;
    user1TaskInfo.feedStartTickMs = STDLIB_COMMON_GetTickMs();
    user1TaskInfo.feedStartWeightG = user1TaskInfo.currentWeightG;
    user1TaskInfo.targetWeightG = user1TaskInfo.currentWeightG + weightG;
    user1TaskInfo.alarmFlags &= (uint8_t)(~USER1_ALARM_MOTOR_JAM);

    __USER1_ServoOpen();
    user1TaskInfo.feedingState = USER1_FEED_STATE_FEEDING;
}

/* 结束当前投喂。
 * normalFinish=1 表示正常完成，会记录历史和今日次数；
 * normalFinish=0 表示异常结束，例如卡堵，只关槽并退出 FEEDING。
 */
static void __USER1_StopFeed(uint8_t normalFinish){
    float actualWeightG;
    user1Plan_t *plan;
    uint8_t planId;

    if(user1TaskInfo.feedingState == USER1_FEED_STATE_IDLE) return;

    __USER1_ServoClose();

    planId = user1TaskInfo.currentPlanId;
    actualWeightG = user1TaskInfo.currentWeightG - user1TaskInfo.feedStartWeightG;

    if(normalFinish != 0U){
        __USER1_AddHistory(planId, actualWeightG);
        user1TaskInfo.feedCountToday++;

        if(user1ManualFeeding == 0U){
            plan = __USER1_FindPlan(planId);
            if(plan != NULL){
                plan->lastFeedTickMs = STDLIB_COMMON_GetTickMs();
                user1PlansDirty = 1U;
            }
        }
    }

    user1TaskInfo.feedingState = USER1_FEED_STATE_IDLE;
    user1TaskInfo.targetWeightG = 0.0f;
    user1ManualFeeding = 0U;
    __USER1_ServoStop();
}

/* 50ms 子任务：处理测试按键。
 * key[0]：切换系统使能，打开后按当前方案执行自动投喂计划；
 * key[1]：切换到下一个已配置投喂方案；
 * key[2]：立即执行一次 50g 测试投喂。 */
static void __USER1_UpdateKeys(void){
    if(__USER1_IsKeyPressEvent(USER1_KEY_ENABLE_INDEX, &user1LastEnableKeyCount) != 0U){
        if(user1TaskInfo.planEnabled == 0U){
            user1TaskInfo.planEnabled = 1U;
            (void)__USER1_GetCurrentPlanOrDefault();
            __USER1_ResetCurrentPlanTimer();
            __USER1_MarkCurrentPlanForFlash();
        } else {
            user1TaskInfo.planEnabled = 0U;
            user1ConfigDirty = 1U;
            if(user1TaskInfo.feedingState != USER1_FEED_STATE_IDLE){
                __USER1_StopFeed(0U);
            }
        }
    }

    if(__USER1_IsKeyPressEvent(USER1_KEY_PLAN_SWITCH_INDEX, &user1LastPlanSwitchKeyCount) != 0U){
        __USER1_SelectPlan(__USER1_FindNextPlanId());
    }

    if(__USER1_IsKeyPressEvent(USER1_KEY_TEST_FEED_INDEX, &user1LastTestFeedKeyCount) != 0U){
        (void)__USER1_GetCurrentPlanOrDefault();
        __USER1_StartFeed(user1TaskInfo.currentPlanId, USER1_KEY_TEST_FEED_WEIGHT_G, 1U);
    }
}

/* 10ms 子任务：使用最近一次缓存重量推进投喂闭环。
 * 达到 targetWeightG - 30g 时提前关槽，例如目标 50g 时检测到 20g 即关槽。 */
static void __USER1_UpdateWeightAndFeedState(void){
    float closeWeightG;
    float targetDeltaG;
    uint32_t nowTickMs;

    user1TaskInfo.currentWeightG = DRIVER_SENSER_GetCurrentWeightG();

    if(user1TaskInfo.feedingState != USER1_FEED_STATE_FEEDING){
        return;
    }

    targetDeltaG = user1TaskInfo.targetWeightG - user1TaskInfo.feedStartWeightG;
    closeWeightG = user1TaskInfo.targetWeightG;
    if(targetDeltaG > USER1_TARGET_CLOSE_ADVANCE_G){
        closeWeightG = user1TaskInfo.targetWeightG - USER1_TARGET_CLOSE_ADVANCE_G;
    }

    if(user1TaskInfo.currentWeightG >= closeWeightG){
        __USER1_StopFeed(1U);
        return;
    }

    nowTickMs = STDLIB_COMMON_GetTickMs();
    if((nowTickMs - user1TaskInfo.feedStartTickMs) > USER1_JAM_TIMEOUT_MS){
        if((user1TaskInfo.currentWeightG - user1TaskInfo.feedStartWeightG) < USER1_JAM_MIN_DELTA_G){
            user1TaskInfo.alarmFlags |= USER1_ALARM_MOTOR_JAM;
            __USER1_StopFeed(0U);
        }
    }
}

/* 100ms 子任务：读取 HX711 并刷新当前重量缓存。
 * HX711 在 RATE=0 时典型输出速率为 10Hz，100ms 读取更贴近有效数据周期。
 */
static void __USER1_UpdateWeightSample(void){
    DRIVER_SENSER_InfoUpdate();
    user1TaskInfo.currentWeightG = DRIVER_SENSER_GetCurrentWeightG();
}

/* 50ms 子任务：慢速传感器接入点。
 * 当前工程没有红外/RFID driver，不能假设 API，因此保持默认状态。
 */
static void __USER1_UpdateSlowSensors(void){
    /* 当前工程没有红外/RFID driver，保持默认值。后续补齐 driver 后在此处读取。 */
    user1TaskInfo.petNearby = 0U;
    user1TaskInfo.rfidPetId = 0U;
}

/* 新增或修改计划：存在同 planId 则覆盖，不存在则写入第一个空槽位。 */
static void __USER1_SetPlan(uint8_t planId, float freqSec, float weightG){
    user1Plan_t *plan;

    if(__USER1_IsValidPlanId(planId) == 0U) return;
    if(__USER1_IsValidPlanParam(freqSec, weightG) == 0U) return;

    plan = __USER1_FindPlan(planId);
    if(plan == NULL){
        plan = __USER1_FindEmptyPlanSlot();
    }
    if(plan == NULL) return;

    plan->planId = planId;
    plan->reserved[0] = 0U;
    plan->reserved[1] = 0U;
    plan->reserved[2] = 0U;
    plan->frequencySec = freqSec;
    plan->weightG = weightG;
    plan->lastFeedTickMs = STDLIB_COMMON_GetTickMs();

    __USER1_SyncPlansToFlashCache();
    user1PlansDirty = 1U;
}

/* 删除计划：只清空对应槽位，不移动其他槽位，便于 APP 按 planId 同步。 */
static void __USER1_DeletePlan(uint8_t planId){
    user1Plan_t *plan = __USER1_FindPlan(planId);

    if(plan == NULL) return;

    plan->planId = 0U;
    plan->reserved[0] = 0U;
    plan->reserved[1] = 0U;
    plan->reserved[2] = 0U;
    plan->frequencySec = 0.0f;
    plan->weightG = 0.0f;
    plan->lastFeedTickMs = 0U;

    if(user1TaskInfo.currentPlanId == planId){
        user1TaskInfo.currentPlanId = 1U;
    }

    __USER1_SyncPlansToFlashCache();
    __USER1_MarkCurrentPlanForFlash();
    user1PlansDirty = 1U;
}

/* 获取 APP 命令码。
 * 需求文档要求命令码来自 RX[2]，但当前 func_appcom 将 CMD 0x13 的 1B 字段
 * 映射到了 remoteInfo.systemEnable，因此这里兼容读取，保证不修改通信框架。
 */
static uint8_t __USER1_GetCmdCode(void){
    uint8_t cmdCode;

    cmdCode = (uint8_t)(remoteInfo.remoteVar_RX[2].var_uint32 & 0xFFU);
    if(cmdCode == USER1_CMD_NONE){
        cmdCode = remoteInfo.systemEnable;
    }
    return cmdCode;
}

/* 100ms 子任务：按 cmdSeq 去重并执行 APP 命令。 */
static void __USER1_ProcessAppCommand(void){
    uint8_t cmdSeq;
    uint8_t cmdCode;
    uint8_t planId;
    float freqSec;
    float weightG;
    float thresholdG;

    cmdSeq = (uint8_t)(remoteInfo.remoteVar_RX[6].var_uint32 & 0xFFU);
    if(cmdSeq == user1TaskInfo.lastCmdSeq){
        return;
    }

    user1TaskInfo.lastCmdSeq = cmdSeq;
    user1LastBleTickMs = STDLIB_COMMON_GetTickMs();
    user1TaskInfo.bleConnected = 1U;

    cmdCode = __USER1_GetCmdCode();
    planId = (uint8_t)(remoteInfo.remoteVar_RX[3].var_uint32 & 0xFFU);
    freqSec = remoteInfo.remoteVar_RX[0].var_float;
    weightG = remoteInfo.remoteVar_RX[1].var_float;
    thresholdG = remoteInfo.remoteVar_RX[4].var_float;

    if(cmdCode != USER1_CMD_NONE){
        user1TaskInfo.alarmFlags &= (uint8_t)(~USER1_ALARM_MOTOR_JAM);
    }

    switch(cmdCode){
    case USER1_CMD_PLAN_SET:
        __USER1_SetPlan(planId, freqSec, weightG);
        break;

    case USER1_CMD_PLAN_DEL:
        __USER1_DeletePlan(planId);
        break;

    case USER1_CMD_SERVO_OPEN:
        if(user1TaskInfo.planEnabled == 0U){
            __USER1_StartFeed(user1TaskInfo.currentPlanId,
                              USER1_MANUAL_TARGET_WEIGHT_G,
                              1U);
        }
        break;

    case USER1_CMD_SERVO_CLOSE:
        if(user1TaskInfo.planEnabled == 0U){
            __USER1_StopFeed(1U);
        }
        break;

    case USER1_CMD_PLAN_ENABLE:
        user1TaskInfo.planEnabled = 1U;
        (void)__USER1_GetCurrentPlanOrDefault();
        __USER1_ResetCurrentPlanTimer();
        __USER1_MarkCurrentPlanForFlash();
        break;

    case USER1_CMD_PLAN_DISABLE:
        user1TaskInfo.planEnabled = 0U;
        user1ConfigDirty = 1U;
        break;

    case USER1_CMD_PLAN_SELECT:
        __USER1_SelectPlan(planId);
        break;

    case USER1_CMD_THRESHOLD_SET:
        if((thresholdG > 0.0f) && (thresholdG < USER1_PLAN_WEIGHT_MAX_G)){
            user1TaskInfo.alarmThresholdG = thresholdG;
            user1ConfigDirty = 1U;
        }
        break;

    default:
        break;
    }
}

/* BLE 连接状态采用命令心跳超时方案。
 * 只要收到新的 cmdSeq 就刷新心跳；超时未收到命令则认为连接不可用。
 */
static void __USER1_UpdateBleState(void){
    uint32_t nowTickMs = STDLIB_COMMON_GetTickMs();

    if((nowTickMs - user1LastBleTickMs) > USER1_BLE_HEARTBEAT_TIMEOUT_MS){
        user1TaskInfo.bleConnected = 0U;
    }
}

/* 100ms 子任务：评估报警位，并将需要蜂鸣的报警转成 boardInfo.buzzTimeMs 请求。 */
static void __USER1_UpdateAlarm(void){
    if(user1TaskInfo.planEnabled == 0U){
        user1TaskInfo.alarmFlags &= (uint8_t)(~USER1_ALARM_LOW_FOOD);
        user1TaskInfo.beeperOn = 0U;
        boardInfo.buzzTimeMs = 0U;
        return;
    }

    if(user1TaskInfo.currentWeightG < user1TaskInfo.alarmThresholdG){
        user1TaskInfo.alarmFlags |= USER1_ALARM_LOW_FOOD;
    } else {
        user1TaskInfo.alarmFlags &= (uint8_t)(~USER1_ALARM_LOW_FOOD);
    }

    __USER1_UpdateBleState();
    if(user1TaskInfo.bleConnected == 0U){
        user1TaskInfo.alarmFlags |= USER1_ALARM_POWER_OFF;
    } else {
        user1TaskInfo.alarmFlags &= (uint8_t)(~USER1_ALARM_POWER_OFF);
    }

    if((user1TaskInfo.alarmFlags & USER1_ALARM_LOW_FOOD) != 0U){
        user1TaskInfo.beeperOn = 1U;
        boardInfo.buzzTimeMs = USER1_BEEP_CONTINUOUS_TIME_MS;
    } else if((user1TaskInfo.alarmFlags & (uint8_t)(~USER1_ALARM_POWER_OFF)) != 0U){
        user1TaskInfo.beeperOn = 1U;
        if(boardInfo.buzzTimeMs == USER1_BEEP_CONTINUOUS_TIME_MS){
            boardInfo.buzzTimeMs = 0U;
        }
        if((user1TaskInfo.taskCnt % USER1_BEEP_PERIOD_TICKS) == 0U){
            boardInfo.buzzTimeMs = USER1_BEEP_TIME_MS;
        }
    } else {
        user1TaskInfo.beeperOn = 0U;
        boardInfo.buzzTimeMs = 0U;
    }
}

/* 500ms 子任务：检查当前计划是否到期，到期后启动投喂。
 * 选择优先级保留为：当前计划 > RFID 对应计划 > 默认方案 1。
 */
static void __USER1_CheckPlanTrigger(void){
    user1Plan_t *plan;
    uint8_t planId;
    uint32_t nowTickMs;
    uint32_t freqMs;

    if(user1TaskInfo.planEnabled == 0U) return;
    if(user1TaskInfo.feedingState != USER1_FEED_STATE_IDLE) return;

    planId = user1TaskInfo.currentPlanId;
    if((planId == 0U) || (__USER1_FindPlan(planId) == NULL)){
        if((user1TaskInfo.rfidPetId != 0U) && (__USER1_FindPlan(user1TaskInfo.rfidPetId) != NULL)){
            planId = user1TaskInfo.rfidPetId;
        } else {
            planId = 1U;
        }
    }

    plan = __USER1_FindPlan(planId);
    if(plan == NULL) return;
    if(__USER1_IsValidPlanParam(plan->frequencySec, plan->weightG) == 0U) return;

    freqMs = (uint32_t)(plan->frequencySec * 1000.0f);
    if(freqMs == 0U) return;

    nowTickMs = STDLIB_COMMON_GetTickMs();
    if((nowTickMs - plan->lastFeedTickMs) >= freqMs){
        __USER1_StartFeed(plan->planId, plan->weightG, 0U);
    }
}

/* 500ms 子任务：写入 APP 可见的状态变量。
 * 注意 TX[0]~TX[3] 为 ESP32 私有/保留区，APP 看不到，因此这里从 TX[4] 开始写。
 */
static void __USER1_UpdateTxStatus(void){
    remoteInfo.remoteVar_TX[4].var_float = user1TaskInfo.currentWeightG;
    remoteInfo.remoteVar_TX[5].var_float = user1TaskInfo.alarmThresholdG;
    remoteInfo.remoteVar_TX[6].var_uint32 = user1TaskInfo.feedingState;
    remoteInfo.remoteVar_TX[7].var_uint32 = user1TaskInfo.alarmFlags;
    remoteInfo.remoteVar_TX[8].var_uint32 = user1TaskInfo.feedCountToday;
    remoteInfo.remoteVar_TX[9].var_uint32 = user1TaskInfo.systemSec;
    remoteInfo.remoteVar_TX[10].var_uint32 = user1TaskInfo.currentPlanId;
    remoteInfo.remoteVar_TX[11].var_uint32 = user1TaskInfo.planEnabled;
}

/* OLED 显示无前导零的无符号整数，用于显示 Flash 版本号等运行参数。 */
static uint8_t __USER1_OledShowUint(uint8_t x, uint8_t y, uint32_t val){
    uint8_t len = 1U;
    uint32_t tmp = val;

    while(tmp >= 10U){
        tmp /= 10U;
        len++;
    }

    DRIVER_OLED_ShowNum(x, y, val, len);
    return (uint8_t)(x + (uint8_t)(len * 6U));
}

/* 500ms 子任务：刷新 OLED 状态页。
 * 128x64 屏幕使用 6x8 字体显示 5 行：
 * Flash 版本号、当前重量、当前计划 ID、下次投喂倒计时/目标重量、报警阈值/报警状态。
 */
static void __USER1_UpdateOled(void){
    uint32_t nextFeedCountdownSec;
    user1Plan_t *plan;
    float planTargetWeightG;
    uint8_t xNext;

    nextFeedCountdownSec = __USER1_GetNextFeedCountdownSec();
    plan = __USER1_GetCurrentPlanOrDefault();
    planTargetWeightG = (plan != NULL) ? plan->weightG : 0.0f;

    DRIVER_OLED_Clear();

    DRIVER_OLED_ShowString(0U, 0U, "FlashV ");
    (void)__USER1_OledShowUint(42U, 0U, flashStore.version);

    DRIVER_OLED_ShowString(0U, 12U, "Weight ");
    DRIVER_OLED_ShowFloat(42U, 12U, user1TaskInfo.currentWeightG, 1U);
    DRIVER_OLED_ShowString(84U, 12U, "g");

    DRIVER_OLED_ShowString(0U, 24U, "Plan ID ");
    DRIVER_OLED_ShowNum(48U, 24U, user1TaskInfo.currentPlanId, 1U);

    DRIVER_OLED_ShowString(0U, 36U, "Next ");
    DRIVER_OLED_ShowNum(30U, 36U, nextFeedCountdownSec, 3U);
    DRIVER_OLED_ShowString(48U, 36U, "s T");
    xNext = DRIVER_OLED_ShowFloat(66U, 36U, planTargetWeightG, 0U);
    DRIVER_OLED_ShowString(xNext, 36U, "g");

    DRIVER_OLED_ShowString(0U, 48U, "Alarm ");
    xNext = DRIVER_OLED_ShowFloat(36U, 48U, user1TaskInfo.alarmThresholdG, 1U);
    DRIVER_OLED_ShowString(xNext, 48U, "g");
    DRIVER_OLED_ShowString((uint8_t)(xNext + 12U), 48U,
                           (user1TaskInfo.alarmFlags != 0U) ? "ALM" : "OK");

    DRIVER_OLED_Refresh();
}

/* 1s 子任务：轮播一个计划槽位给 APP。
 * APP 每 5 秒可以收齐 5 个槽位，planId=0 表示空槽。
 */
static void __USER1_BroadcastPlan(void){
    user1Plan_t *plan;

    if(user1TaskInfo.planBroadcastIndex >= USER1_PLAN_MAX_COUNT){
        user1TaskInfo.planBroadcastIndex = 0U;
    }

    plan = &user1TaskInfo.plans[user1TaskInfo.planBroadcastIndex];
    remoteInfo.remoteVar_TX[12].var_float = plan->frequencySec;
    remoteInfo.remoteVar_TX[13].var_float = plan->weightG;
    remoteInfo.remoteVar_TX[14].var_uint32 = plan->planId;
    remoteInfo.remoteVar_TX[15].var_uint32 = USER1_PLAN_MAX_COUNT;

    user1TaskInfo.planBroadcastIndex++;
    if(user1TaskInfo.planBroadcastIndex >= USER1_PLAN_MAX_COUNT){
        user1TaskInfo.planBroadcastIndex = 0U;
    }
}

/* Flash 维护：不改 userLib 结构，只复用现有 version 字段保存上一次方案 ID。 */
/* systemTask 每次写 Flash 前调用，确保 flashStore 里总是最新投喂计划表。 */
void USER1_FlashStorePrepare(void){
    if(user1FlashStoreReady == 0U) return;

    /* dirty 标志用于表达“已有业务变化”，但保存前始终生成完整快照。 */
    if((user1PlansDirty != 0U) || (user1ConfigDirty != 0U)){
        user1PlansDirty = 0U;
        user1ConfigDirty = 0U;
    }

    __USER1_SyncPlansToFlashCache();
    if(__USER1_IsValidPlanId(user1TaskInfo.currentPlanId) != 0U){
        flashStore.lastPlanId = (uint32_t)user1TaskInfo.currentPlanId;
    }
}

static void __USER1_FlashMaintenance(void){
    /* version 不在这里修改，STDLIB_FLASH_Save() 每次实际写入前自增。 */
    /* 不改 Flash 底层结构，只把“上一次选择的方案 ID”放入现有 version 字段。
     * systemTask 已经周期调用 STDLIB_FLASH_Save()，这里不直接擦写 Flash。 */
    USER1_FlashStorePrepare();

    (void)user1HistoryDirty;
}

/* 等待 systemTask 完成 STDLIB_FLASH_Init()，避免 user1Task 先用空 flashStore 覆盖已保存计划。 */
static void __USER1_WaitSystemFlashReady(void){
    uint32_t waitMs = 0U;

    while((systemTaskInfo.systemInitFinshFlag == 0U) &&
          (waitMs < USER1_WAIT_SYSTEM_INIT_MS)){
        osDelay(10U);
        waitMs += 10U;
    }
}

/*============================================================================
 * API
 *============================================================================*/
void user1TaskInit(void){
    memset(&user1TaskInfo, 0, sizeof(user1TaskInfo));
    user1FlashStoreReady = 0U;

    user1TaskInfo.alarmThresholdG = USER1_DEFAULT_ALARM_THRESHOLD_G;
    user1TaskInfo.planEnabled = 0U;
    user1TaskInfo.lastCmdSeq = 0xFFU;
    user1TaskInfo.bleConnected = 1U;
    user1LastBleTickMs = STDLIB_COMMON_GetTickMs();
    __USER1_WaitSystemFlashReady();
    if(__USER1_LoadPlansFromFlash() == 0U){
        __USER1_InitDefaultPlan1();
    }
    if(__USER1_FindPlan(1U) == NULL){
        __USER1_InitDefaultPlan1();
    }
    user1TaskInfo.currentPlanId = __USER1_LoadLastPlanIdFromFlash();
    if(__USER1_FindPlan(user1TaskInfo.currentPlanId) == NULL){
        user1TaskInfo.currentPlanId = 1U;
    }
    __USER1_SyncPlansToFlashCache();
    __USER1_MarkCurrentPlanForFlash();
    user1FlashStoreReady = 1U;

    DRIVER_SENSER_Init();
    __USER1_ServoStop();
    boardInfo.buzzTimeMs = 0U;
    user1LastEnableKeyCount = boardInfo.key[USER1_KEY_ENABLE_INDEX].pressCount;
    user1LastPlanSwitchKeyCount = boardInfo.key[USER1_KEY_PLAN_SWITCH_INDEX].pressCount;
    user1LastTestFeedKeyCount = boardInfo.key[USER1_KEY_TEST_FEED_INDEX].pressCount;

    osDelay(100);
    DRIVER_OLED_Init();
}

void user1TaskUpdata(void *argument){
    (void)argument;

    user1TaskInit();
    for(;;){
        user1TaskInfo.taskCnt++;

        if(user1TaskInfo.taskCnt % 5U == 0U){
            /* 10ms：重量闭环、卡堵检测 */
            __USER1_UpdateWeightAndFeedState();
        }

        if(user1TaskInfo.taskCnt % 25U == 0U){
            /* 50ms：红外/RFID 慢速传感器，占位等待 driver 接入 */
            __USER1_UpdateSlowSensors();
            __USER1_UpdateKeys();
        }

        if(user1TaskInfo.taskCnt % 50U == 0U){
            /* 100ms：称重采样、APP 命令、报警和蜂鸣器请求 */
            __USER1_UpdateWeightSample();
            __USER1_ProcessAppCommand();
            __USER1_UpdateAlarm();
        }

        if(user1TaskInfo.taskCnt % 250U == 0U){
            /* 500ms：计划触发检查、状态上报、OLED 刷新 */
            __USER1_CheckPlanTrigger();
            __USER1_UpdateTxStatus();
            __USER1_UpdateOled();
        }

        if(user1TaskInfo.taskCnt % 500U == 0U){
            /* 1s：系统秒计数、计划轮播、持久化维护 */
            user1TaskInfo.systemSec++;
            __USER1_BroadcastPlan();
            __USER1_FlashMaintenance();
        }

        osDelay(2);
    }
}
