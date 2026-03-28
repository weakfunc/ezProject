#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

/* ============================================================
 * systemConfig_t — 系统全局配置结构体
 * ============================================================ */
typedef struct {
    uint32_t sys_time_s;    /* 系统运行时间（秒），自上电起累计 */
} systemConfig_t;

extern systemConfig_t systemConfig;

#endif /* MAIN_H */
