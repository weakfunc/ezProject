#ifndef __DRIVER_DS3231RTC_H__
#define __DRIVER_DS3231RTC_H__

#include <stdint.h>
#include "stdlib_i2c.h"

/*============================================================================
 * 向下依赖（driver层向stdlib层索要）
 * 依赖：stdlib_i2c
 *============================================================================*/

/* DS3231 使用的 I2C 总线编号，默认 I2C_BUS_1 */
#define DS3231RTC_DEP_I2C_BUS_ID                 I2C_BUS_1

/* DS3231 7位I2C地址（固定，不可更改） */
#define DS3231RTC_CFG_ADDR_7BIT                  (0x68U)

/* 含写位的8位I2C地址：0x68 << 1 = 0xD0 */
#define DS3231RTC_CFG_ADDR_WRITE                 ((uint8_t)(DS3231RTC_CFG_ADDR_7BIT << 1U))

/* I2C 读取：从寄存器 reg 读取 len 字节到 buf */
#define DS3231RTC_DEP_I2C_READ(reg, buf, len) \
  STDLIB_I2C_ReadData(DS3231RTC_DEP_I2C_BUS_ID, DS3231RTC_CFG_ADDR_WRITE, (uint8_t)(reg), (buf), (uint16_t)(len))

/* I2C 写入：向寄存器 reg 连续写入 len 字节数据 data */
#define DS3231RTC_DEP_I2C_WRITE(reg, data, len) \
  STDLIB_I2C_WriteData(DS3231RTC_DEP_I2C_BUS_ID, DS3231RTC_CFG_ADDR_WRITE, (uint8_t)(reg), (data), (uint16_t)(len))

/*============================================================================
 * 向上提供（driver层向func/task层提供）
 *============================================================================*/

/* BCD 与十进制互转宏，供上层解析 ds3231RTCTime_t 各字段使用。
 * 结构体各字段存储寄存器原始 BCD 值，读取后需用 DS3231RTC_BCD_TO_DEC 转为十进制显示。
 */
#define DS3231RTC_BCD_TO_DEC(x)  (((x) >> 4U) * 10U + ((x) & 0x0FU))
#define DS3231RTC_DEC_TO_BCD(x)  ((((x) / 10U) << 4U) | ((x) % 10U))

/* DS3231 时间数据（各字段存储寄存器原始 BCD 值，使用前需用 DS3231RTC_BCD_TO_DEC 转换） */
typedef struct {
  uint8_t second;  /* 秒，BCD，0x00~0x59 */
  uint8_t minute;  /* 分，BCD，0x00~0x59 */
  uint8_t hour;    /* 时，BCD，0x00~0x23（24小时制，bit6=0） */
  uint8_t day;     /* 星期，1~7（1=周日） */
  uint8_t date;    /* 日，BCD，0x01~0x31 */
  uint8_t month;   /* 月，BCD，0x01~0x12 */
  uint8_t year;    /* 年，BCD，0x00~0x99（相对2000年，如0x26表示2026） */
} ds3231RTCTime_t;

/* DS3231 模块公有信息结构体 */
typedef struct {
  ds3231RTCTime_t time;  /* 当前时间（BCD原始值） */
  uint8_t isReady;       /* 数据就绪标志（1=已完成首次读取，0=未就绪） */
} ds3231RTCInfo_t;

extern ds3231RTCInfo_t ds3231RTCInfo;

/* 初始化DS3231 RTC驱动，向芯片写入初始时间（2026-04-29 19:19:00）并初始化I2C总线 */
void DRIVER_DS3231RTC_Init(void);

/* 从DS3231读取当前时间（BCD原始值）并更新ds3231RTCInfo.time，需周期性调用 */
void DRIVER_DS3231RTC_Update(void);

/* [修改] 原因：KEY2 需要将 DS3231 时间写回最近整点 */
void DRIVER_DS3231RTC_SetTime(const ds3231RTCTime_t *time);

#endif
