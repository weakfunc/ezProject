#include "driver_ds3231rtc.h"

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* 时间寄存器起始地址（连续7字节：秒-分-时-星期-日-月-年） */
#define DS3231RTC_REG_SECONDS      (0x00U)

/* 时间寄存器帧字节数 */
#define DS3231RTC_TIME_REG_COUNT   (7U)

/* 各字节在读取缓冲区中的偏移 */
#define DS3231RTC_IDX_SECOND       (0U)
#define DS3231RTC_IDX_MINUTE       (1U)
#define DS3231RTC_IDX_HOUR         (2U)
#define DS3231RTC_IDX_DAY          (3U)
#define DS3231RTC_IDX_DATE         (4U)
#define DS3231RTC_IDX_MONTH        (5U)
#define DS3231RTC_IDX_YEAR         (6U)

/* 秒寄存器：bit7为时钟停止位，屏蔽后保留BCD值 */
#define DS3231RTC_SECOND_MASK      (0x7FU)
/* 分寄存器：bit7保留位，屏蔽后保留BCD值 */
#define DS3231RTC_MINUTE_MASK      (0x7FU)
/* 时寄存器：bit6为12/24标志（0=24h），屏蔽bit6/7后保留BCD值 */
#define DS3231RTC_HOUR_MASK        (0x3FU)
/* 日期寄存器：bit7-6保留位 */
#define DS3231RTC_DATE_MASK        (0x3FU)
/* 月份寄存器：bit7为世纪位，屏蔽后保留BCD值 */
#define DS3231RTC_MONTH_MASK       (0x1FU)
/* 星期寄存器：bit2-0有效 */
#define DS3231RTC_DAY_MASK         (0x07U)

/*============================================================================
 * 私有变量
 *============================================================================*/

/* DS3231 公有信息结构体（extern 声明在 .h 中） */
ds3231RTCInfo_t ds3231RTCInfo;

/*============================================================================
 * API 接口
 *============================================================================*/

/* 初始化 DS3231 RTC 驱动：初始化 I2C 总线，向芯片写入初始时间 2026-04-29 19:19:00。
 * 写入后清零本地结构体，等待首次 Update 读取刷新。
 */
void DRIVER_DS3231RTC_Init(void){
  /* 初始时间 BCD 编码（2026-04-29 周三 19:19:00，1=周日） */
  static const uint8_t initTime[DS3231RTC_TIME_REG_COUNT] = {
    0x00U,  /* 秒：00 */
    0x19U,  /* 分：19 */
    0x19U,  /* 时：19（24小时制，bit6=0） */
    0x04U,  /* 星期：4（周三，1=周日） */
    0x29U,  /* 日：29 */
    0x04U,  /* 月：04 */
    0x26U,  /* 年：26（即2026） */
  };

  STDLIB_I2C_Init();
  DS3231RTC_DEP_I2C_WRITE(DS3231RTC_REG_SECONDS, initTime, DS3231RTC_TIME_REG_COUNT);

  ds3231RTCInfo.isReady     = 0U;
  ds3231RTCInfo.time.second = 0U;
  ds3231RTCInfo.time.minute = 0U;
  ds3231RTCInfo.time.hour   = 0U;
  ds3231RTCInfo.time.day    = 0U;
  ds3231RTCInfo.time.date   = 0U;
  ds3231RTCInfo.time.month  = 0U;
  ds3231RTCInfo.time.year   = 0U;
}

/* 从 DS3231 读取连续 7 字节时间寄存器，屏蔽无关位后以 BCD 原始值存入 ds3231RTCInfo.time。
 * 首次读取后将 isReady 置 1，需由上层每秒调用一次。
 * 上层使用 DS3231RTC_BCD_TO_DEC 宏将字段转为十进制。
 */
void DRIVER_DS3231RTC_Update(void){
  uint8_t buf[DS3231RTC_TIME_REG_COUNT];

  DS3231RTC_DEP_I2C_READ(DS3231RTC_REG_SECONDS, buf, DS3231RTC_TIME_REG_COUNT);

  ds3231RTCInfo.time.second = buf[DS3231RTC_IDX_SECOND] & DS3231RTC_SECOND_MASK;
  ds3231RTCInfo.time.minute = buf[DS3231RTC_IDX_MINUTE] & DS3231RTC_MINUTE_MASK;
  ds3231RTCInfo.time.hour   = buf[DS3231RTC_IDX_HOUR]   & DS3231RTC_HOUR_MASK;
  ds3231RTCInfo.time.day    = buf[DS3231RTC_IDX_DAY]    & DS3231RTC_DAY_MASK;
  ds3231RTCInfo.time.date   = buf[DS3231RTC_IDX_DATE]   & DS3231RTC_DATE_MASK;
  ds3231RTCInfo.time.month  = buf[DS3231RTC_IDX_MONTH]  & DS3231RTC_MONTH_MASK;
  ds3231RTCInfo.time.year   = buf[DS3231RTC_IDX_YEAR];

  ds3231RTCInfo.isReady = 1U;
}

/* [修改] 原因：KEY2 需要将 DS3231 时间写回最近整点。
 * 写入参数沿用 ds3231RTCTime_t 的 BCD 原始格式，写入后同步更新公有信息结构体。
 */
void DRIVER_DS3231RTC_SetTime(const ds3231RTCTime_t *time){
  uint8_t buf[DS3231RTC_TIME_REG_COUNT];

  if(time == NULL){
    return;
  }

  buf[DS3231RTC_IDX_SECOND] = time->second & DS3231RTC_SECOND_MASK;
  buf[DS3231RTC_IDX_MINUTE] = time->minute & DS3231RTC_MINUTE_MASK;
  buf[DS3231RTC_IDX_HOUR]   = time->hour   & DS3231RTC_HOUR_MASK;
  buf[DS3231RTC_IDX_DAY]    = time->day    & DS3231RTC_DAY_MASK;
  buf[DS3231RTC_IDX_DATE]   = time->date   & DS3231RTC_DATE_MASK;
  buf[DS3231RTC_IDX_MONTH]  = time->month  & DS3231RTC_MONTH_MASK;
  buf[DS3231RTC_IDX_YEAR]   = time->year;

  DS3231RTC_DEP_I2C_WRITE(DS3231RTC_REG_SECONDS, buf, DS3231RTC_TIME_REG_COUNT);

  ds3231RTCInfo.time.second = buf[DS3231RTC_IDX_SECOND];
  ds3231RTCInfo.time.minute = buf[DS3231RTC_IDX_MINUTE];
  ds3231RTCInfo.time.hour   = buf[DS3231RTC_IDX_HOUR];
  ds3231RTCInfo.time.day    = buf[DS3231RTC_IDX_DAY];
  ds3231RTCInfo.time.date   = buf[DS3231RTC_IDX_DATE];
  ds3231RTCInfo.time.month  = buf[DS3231RTC_IDX_MONTH];
  ds3231RTCInfo.time.year   = buf[DS3231RTC_IDX_YEAR];
  ds3231RTCInfo.isReady     = 1U;
}
