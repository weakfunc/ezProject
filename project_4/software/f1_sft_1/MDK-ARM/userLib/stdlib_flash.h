#ifndef __STDLIB_FLASH_H__
#define __STDLIB_FLASH_H__

#include "main.h"
#include <stdint.h>

/*============================================================================
 * 内部配置
 *============================================================================*/

/* Flash 末页基地址（STM32F103C8T6 共 64 KB，末页起始地址 0x0800FC00）。
 * 若程序超 63 KB，需将此值改为 0x0800F800（往前移一页）。 */
#define USER_FLASH_STORE_PAGE_ADDR  (0x0800FC00U)

/* Flash 页大小（字节）。STM32F103C8T6 每页 1 KB。 */
#define USER_FLASH_PAGE_SIZE        (1024U)

/* 魔术字，用于判断 Flash 数据是否有效。 */
#define USER_FLASH_MAGIC_WORD       (0xA55A1234U)

/* flashStore_t 数据体最大允许字节数（页大小 - 魔术字 4B）。 */
#define USER_FLASH_STORE_DATA_MAX   (USER_FLASH_PAGE_SIZE - 4U)

/*============================================================================
 * API 接口
 *============================================================================*/

/* 持久化数据布局 — 按项目需求在此添加字段，总大小不超过 1020 字节。 */
typedef struct {
  uint32_t version;           /* 存档版本号，每次写入前由调用方自增 */

  /* user1TaskInfo_t 镜像 */
  uint32_t taskCnt;           /* 任务计数器 */
  float    currentTemp;       /* 当前水温（℃） */
  float    targetTemp;        /* 目标水温（℃） */
  float    pidIntegral;       /* PID 积分项累计值 */
  float    pidLastError;      /* PID 上一周期误差 */
  uint8_t  heaterOn;          /* 加热输出状态：0=关，1=开 */
  uint8_t  overTempFlag;      /* 超温保护标志：0=正常，1=超温 */
  uint8_t  dryBurnFlag;       /* 防干烧标志：0=有水，1=缺水 */
  uint8_t  massageMode;       /* 当前实际按摩模式 */
  uint8_t  massageModeTarget; /* 目标按摩模式 */
  uint32_t motorPwmDuty;      /* 电机占空比百分比，0~100 */
  uint32_t timerSetSec;       /* 定时总时长（秒） */
  uint32_t timerRemainSec;    /* 定时剩余时长（秒） */
  uint8_t  timerActive;       /* 定时激活标志：0=否，1=是 */
  uint8_t  sysRunning;        /* 系统运行状态：0=待机，1=运行 */
  uint8_t  faultCode;         /* 故障码 */
} flashStore_t;

/* 公有存储区，所有模块直接读写 */
extern flashStore_t flashStore;

/* 从 Flash 加载数据到 flashStore；魔术字不匹配则写入默认值（全零）。 */
void    STDLIB_FLASH_Init(void);

/* 将当前 flashStore 写入 Flash（擦页 + 半字写入）。 */
void    STDLIB_FLASH_Save(void);

/* flashStore 全零后写入 Flash。 */
void    STDLIB_FLASH_Reset(void);

/* 返回 1=上次 Init 读到有效数据，0=使用了默认值。 */
uint8_t STDLIB_FLASH_IsValid(void);

#endif
