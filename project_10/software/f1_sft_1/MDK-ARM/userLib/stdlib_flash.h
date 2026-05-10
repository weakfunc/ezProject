#ifndef __STDLIB_FLASH_H__
#define __STDLIB_FLASH_H__

#include "main.h"
#include <stdint.h>

/*============================================================================
 * 内部配置
 *============================================================================*/

/* Flash 末页基地址：STM32F103C8T6 64 KB 时末页起始地址为 0x0800FC00。 */
#define USER_FLASH_STORE_PAGE_ADDR  (0x0800FC00U)

/* Flash 页大小：STM32F103C8T6 每页 1 KB。 */
#define USER_FLASH_PAGE_SIZE        (1024U)

/* 魔术字，用于判断 Flash 数据是否有效。 */
#define USER_FLASH_MAGIC_WORD       (0xA55A1234U)

/* flashStore_t 最大允许字节数：页大小 - 魔术字 4B。 */
#define USER_FLASH_STORE_DATA_MAX   (USER_FLASH_PAGE_SIZE - 4U)

/* Flash 内保存的投喂计划槽位数量，与 user1TaskInfo.plans[] 保持一致。 */
#define USER_FLASH_PLAN_MAX_COUNT   (5U)

/*============================================================================
 * API 接口
 *============================================================================*/

/* Flash 内保存的单条投喂计划。
 * lastFeedTickMs 会保存用于完整留档；上电恢复时应用层会按当前 tick 重新校准。 */
typedef struct {
  uint8_t  planId;          /* 计划 ID，1~5；0 表示空槽 */
  uint8_t  reserved[3];     /* 对齐填充 */
  float    frequencySec;    /* 投喂频率，单位 s */
  float    weightG;         /* 单次投喂目标重量，单位 g */
  uint32_t lastFeedTickMs;  /* 上次投喂 tick；上电恢复时会重新校准 */
} flashFeedPlan_t;

/* 持久化数据布局，总大小不得超过 USER_FLASH_STORE_DATA_MAX。 */
typedef struct {
  uint32_t        version;                          /* 存档版本号，每次 STDLIB_FLASH_Save() 自动 +1 */
  uint32_t        lastPlanId;                       /* 上一次选择的投喂计划 ID */
  flashFeedPlan_t plans[USER_FLASH_PLAN_MAX_COUNT]; /* 投喂计划表 */
} flashStore_t;

extern flashStore_t flashStore;

/* 从 Flash 加载数据到 flashStore；魔术字不匹配则写入默认值。 */
void    STDLIB_FLASH_Init(void);

/* 将当前 flashStore 写入 Flash；写入前自动 version++。 */
void    STDLIB_FLASH_Save(void);

/* flashStore 全零后写入 Flash。 */
void    STDLIB_FLASH_Reset(void);

/* 返回 1=上次 Init 读到有效数据，0=使用了默认值。 */
uint8_t STDLIB_FLASH_IsValid(void);

#endif
