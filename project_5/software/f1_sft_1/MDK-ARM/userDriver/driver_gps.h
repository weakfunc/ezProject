#ifndef __DRIVER_GPS_H__
#define __DRIVER_GPS_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 *============================================================================*/
#define GPS_DEP_UART_PORT                      UART_PORT3
#define GPS_DEP_UART_SET_CUSTOM_CB(cb)         STDLIB_USART_SetCustomCb(GPS_DEP_UART_PORT, (cb))

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/
#define GPS_NMEA_LINE_MAX_LEN                  (96U)
#define GPS_FIELD_MAX                          (20U)

/* GPS UTC时间信息 */
typedef struct {
    uint8_t hour;         /* 时（0~23） */
    uint8_t minute;       /* 分（0~59） */
    uint8_t second;       /* 秒（0~59） */
    uint16_t millisecond; /* 毫秒（0~999） */
    uint8_t day;          /* 日（1~31） */
    uint8_t month;        /* 月（1~12） */
    uint16_t year;        /* 年（如2025） */
} gpsUtcTime_t;

/* GPS连接状态枚举 */
typedef enum {
    GPS_STATUS_DISCONNECTED   = 0U,  /* 未连接：无时间无航向 */
    GPS_STATUS_SEARCHING      = 1U,  /* 已连接，搜星中：有时间无航向 */
    GPS_STATUS_WORKING        = 2U,  /* 已连接，正常工作：有时间有航向 */
} gpsStatus_t;

/* GPS常用导航信息 */
typedef struct {
    float latitudeDeg;    /* 纬度（十进制度，南纬为负） */
    float longitudeDeg;   /* 经度（十进制度，西经为负） */
    float speedKnots;     /* 速度（节） */
    float speedKmh;       /* 速度（km/h） */
    float courseDeg;      /* 航向角（度，0~360，正北为0） */
    gpsUtcTime_t utc;     /* UTC时间 */
    uint8_t fixValid;     /* 定位有效标志（1=有效，0=无效） */
    uint8_t hasLocation;  /* 是否已获取有效经纬度（1=是，0=否） */
    uint8_t hasTime;      /* 是否已获取有效UTC时间（1=是，0=否） */
    uint8_t hasCourse;    /* 是否已获取有效航向角（1=是，0=否） */
    gpsStatus_t status;   /* GPS连接与定位状态 */
} gpsInfo_t;

extern gpsInfo_t gpsInfo;

/* 初始化GPS驱动并绑定USART3字节回调 */
void DRIVER_GPS_Init(void);

/* 清空GPS缓存数据与解析状态 */
void DRIVER_GPS_Reset(void);

/* 查询是否有新GPS数据到达 */
uint8_t DRIVER_GPS_HasNewData(void);

/* 获取最新GPS信息快照 */
uint8_t DRIVER_GPS_GetInfo(gpsInfo_t *info);

/* 获取最新纬度（十进制度） */
uint8_t DRIVER_GPS_GetPosition(float *longitudeDeg, float *latitudeDeg);

/* 获取最新UTC时间 */
uint8_t DRIVER_GPS_GetUtcTime(gpsUtcTime_t *utcTime);

/* 获取最新航向角（单位：度） */
uint8_t DRIVER_GPS_GetCourseDeg(float *courseDeg);

#endif
