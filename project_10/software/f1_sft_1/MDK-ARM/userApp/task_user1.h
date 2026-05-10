#ifndef __TASK_USER1_H__
#define __TASK_USER1_H__

#include "main.h"
#include "cmsis_os.h"

/* 投喂方案最大数量 */
#define USER1_PLAN_MAX_COUNT      (5U)

/* 命令码（来自 APP 的 RX[2]，当前 func_appcom 将该 1B 字段映射到 remoteInfo.systemEnable） */
#define USER1_CMD_NONE            (0U)
#define USER1_CMD_PLAN_SET        (1U)
#define USER1_CMD_PLAN_DEL        (2U)
#define USER1_CMD_SERVO_OPEN      (3U)
#define USER1_CMD_SERVO_CLOSE     (4U)
#define USER1_CMD_PLAN_ENABLE     (5U)
#define USER1_CMD_PLAN_DISABLE    (6U)
#define USER1_CMD_PLAN_SELECT     (7U)
#define USER1_CMD_THRESHOLD_SET   (8U)

/* 投喂状态 */
#define USER1_FEED_STATE_IDLE     (0U)
#define USER1_FEED_STATE_FEEDING  (1U)

/* 报警标志位 */
#define USER1_ALARM_LOW_FOOD      (1U << 0)
#define USER1_ALARM_MOTOR_JAM     (1U << 1)
#define USER1_ALARM_POWER_OFF     (1U << 2)

/* 历史记录最大数量 */
#define USER1_HISTORY_MAX_COUNT   (30U)

/* 单条投喂方案 */
typedef struct {
    uint8_t  planId;          /* 方案ID 1~5，0 表示该槽位未使用 */
    uint8_t  reserved[3];     /* 对齐填充 */
    float    frequencySec;    /* 投喂频率，单位秒 */
    float    weightG;         /* 单次投喂目标重量，单位克 */
    uint32_t lastFeedTickMs;  /* 上次投喂 tick，用于计算下次投喂时间 */
} user1Plan_t;

/* 历史投喂记录单条 */
typedef struct {
    uint8_t  planId;          /* 触发该次投喂的方案ID */
    uint8_t  reserved[3];
    float    actualWeightG;   /* 实际投喂重量，单位克 */
    uint32_t timestampSec;    /* 投喂时间戳，使用系统秒计数 */
} user1FeedRecord_t;

typedef struct user1TaskInfo {
    /* 任务节拍 */
    uint32_t taskCnt;                 /* 任务计数，基础周期 2ms */

    /* 传感器数据 */
    float    currentWeightG;          /* 当前食槽重量，单位 g */
    uint8_t  petNearby;               /* 红外检测到宠物接近：0=否，1=是 */
    uint8_t  rfidPetId;               /* RFID 识别到的宠物ID，0=未识别 */

    /* 投喂控制 */
    uint8_t  feedingState;            /* 当前投喂状态 */
    uint8_t  planEnabled;             /* 计划执行开关，0=禁用，1=启用 */
    uint8_t  currentPlanId;           /* 当前执行方案ID，0=无 */
    float    targetWeightG;           /* 当前投喂目标重量 */
    uint32_t feedStartTickMs;         /* 本次投喂开始 tick */
    float    feedStartWeightG;        /* 本次投喂开始重量 */

    /* 投喂方案列表 */
    user1Plan_t plans[USER1_PLAN_MAX_COUNT];

    /* 历史记录环形缓冲 */
    user1FeedRecord_t history[USER1_HISTORY_MAX_COUNT];
    uint8_t  historyHead;             /* 环形缓冲写入位置 */
    uint8_t  historyCount;            /* 已存数量 */

    /* 报警 */
    float    alarmThresholdG;         /* 食槽余量报警阈值，单位 g */
    uint8_t  alarmFlags;              /* 报警标志位组合 */
    uint8_t  beeperOn;                /* 蜂鸣器当前是否鸣响 */

    /* 计划同步广播 */
    uint8_t  planBroadcastIndex;      /* 当前广播的方案索引 */

    /* 命令去重 */
    uint8_t  lastCmdSeq;              /* 上次执行的 RX[6] 序列号 */

    /* 系统计时 */
    uint32_t systemSec;               /* 系统秒计数 */
    uint32_t feedCountToday;          /* 当日投喂次数 */

    /* BLE 连接状态 */
    uint8_t  bleConnected;            /* 0=未连接，1=已连接 */
} user1TaskInfo_t;

extern user1TaskInfo_t user1TaskInfo;

void user1TaskInit(void);
void user1TaskUpdata(void *argument);
void USER1_FlashStorePrepare(void);

#endif
