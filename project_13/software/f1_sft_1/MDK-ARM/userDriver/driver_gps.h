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
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t millisecond;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} gpsUtcTime_t;

/* GPS常用导航信息 */
typedef struct {
    float latitudeDeg;
    float longitudeDeg;
    float speedKnots;
    float speedKmh;
    float courseDeg;
    gpsUtcTime_t utc;
    uint8_t fixValid;
    uint8_t hasLocation;
    uint8_t hasTime;
    uint8_t hasCourse;
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
uint8_t DRIVER_GPS_GetLatitudeDeg(float *latitudeDeg);

/* 获取最新经度（十进制度） */
uint8_t DRIVER_GPS_GetLongitudeDeg(float *longitudeDeg);

/* 获取最新UTC时间 */
uint8_t DRIVER_GPS_GetUtcTime(gpsUtcTime_t *utcTime);

/* 获取最新航向角（单位：度） */
uint8_t DRIVER_GPS_GetCourseDeg(float *courseDeg);

#endif
