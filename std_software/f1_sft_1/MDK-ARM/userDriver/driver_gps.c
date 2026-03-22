#include "driver_gps.h"
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * 内部配置（仅driver_gps模块内部使用）
 *============================================================================*/

/* GPS最新数据缓存 */
static gpsInfo_t gpsInfoCache;
/* GPS数据更新标志 */
static uint8_t gpsHasNewDataFlag = 0U;
/* NMEA单行缓冲区 */
static char gpsNmeaLineBuf[GPS_NMEA_LINE_MAX_LEN];
/* NMEA当前写入索引 */
static uint8_t gpsNmeaLineIdx = 0U;
/* NMEA接收状态 */
static uint8_t gpsNmeaReceiving = 0U;

/* 将十六进制字符转换为4bit数值 */
static uint8_t __DRIVER_GPS_HexCharToNibble(char ch, uint8_t *nibble){
    if((ch >= '0') && (ch <= '9')){
        *nibble = (uint8_t)(ch - '0');
        return 1U;
    }
    if((ch >= 'A') && (ch <= 'F')){
        *nibble = (uint8_t)(ch - 'A' + 10);
        return 1U;
    }
    if((ch >= 'a') && (ch <= 'f')){
        *nibble = (uint8_t)(ch - 'a' + 10);
        return 1U;
    }
    return 0U;
}

/* 解析2位十六进制校验和 */
static uint8_t __DRIVER_GPS_ParseChecksumHex(const char *hexStr, uint8_t *checksum){
    uint8_t high;
    uint8_t low;

    if((hexStr == NULL) || (checksum == NULL)) return 0U;
    if(__DRIVER_GPS_HexCharToNibble(hexStr[0], &high) == 0U) return 0U;
    if(__DRIVER_GPS_HexCharToNibble(hexStr[1], &low) == 0U) return 0U;

    *checksum = (uint8_t)((high << 4U) | low);
    return 1U;
}

/* 校验NMEA语句校验和 */
static uint8_t __DRIVER_GPS_VerifyNmeaChecksum(const char *line){
    const char *starPos;
    uint8_t calcChecksum = 0U;
    uint8_t recvChecksum = 0U;

    if(line == NULL) return 0U;
    if(line[0] != '$') return 0U;

    starPos = strchr(line, '*');
    if(starPos == NULL) return 0U;
    if((starPos[1] == '\0') || (starPos[2] == '\0')) return 0U;

    for(const char *p = &line[1]; p < starPos; p++){
        calcChecksum ^= (uint8_t)(*p);
    }

    if(__DRIVER_GPS_ParseChecksumHex(starPos + 1, &recvChecksum) == 0U) return 0U;
    if(calcChecksum != recvChecksum) return 0U;
    return 1U;
}

/* 解析2位十进制数字 */
static uint8_t __DRIVER_GPS_Parse2Digit(const char *str, uint8_t *value){
    if((str == NULL) || (value == NULL)) return 0U;
    if((str[0] < '0') || (str[0] > '9')) return 0U;
    if((str[1] < '0') || (str[1] > '9')) return 0U;

    *value = (uint8_t)(((str[0] - '0') * 10) + (str[1] - '0'));
    return 1U;
}

/* 将字符串转换为浮点数 */
static uint8_t __DRIVER_GPS_ParseFloat(const char *str, float *value){
    char *endPtr;
    float parsed;

    if((str == NULL) || (value == NULL)) return 0U;
    if(str[0] == '\0') return 0U;

    parsed = strtof(str, &endPtr);
    if(endPtr == str) return 0U;

    *value = parsed;
    return 1U;
}

/* 解析UTC时间字段（hhmmss.sss） */
static uint8_t __DRIVER_GPS_ParseUtcTimeField(const char *timeStr, gpsUtcTime_t *utcTime){
    uint16_t ms = 0U;
    uint8_t msDigits = 0U;
    uint8_t i;

    if((timeStr == NULL) || (utcTime == NULL)) return 0U;
    if(strlen(timeStr) < 6U) return 0U;

    if(__DRIVER_GPS_Parse2Digit(&timeStr[0], &utcTime->hour) == 0U) return 0U;
    if(__DRIVER_GPS_Parse2Digit(&timeStr[2], &utcTime->minute) == 0U) return 0U;
    if(__DRIVER_GPS_Parse2Digit(&timeStr[4], &utcTime->second) == 0U) return 0U;

    if(timeStr[6] == '.'){
        i = 7U;
        while((timeStr[i] >= '0') && (timeStr[i] <= '9') && (msDigits < 3U)){
            ms = (uint16_t)(ms * 10U + (uint16_t)(timeStr[i] - '0'));
            msDigits++;
            i++;
        }
        while(msDigits < 3U){
            ms = (uint16_t)(ms * 10U);
            msDigits++;
        }
    }

    utcTime->millisecond = ms;
    return 1U;
}

/* 解析UTC日期字段（ddmmyy） */
static uint8_t __DRIVER_GPS_ParseUtcDateField(const char *dateStr, gpsUtcTime_t *utcTime){
    uint8_t yearLow;

    if((dateStr == NULL) || (utcTime == NULL)) return 0U;
    if(strlen(dateStr) < 6U) return 0U;

    if(__DRIVER_GPS_Parse2Digit(&dateStr[0], &utcTime->day) == 0U) return 0U;
    if(__DRIVER_GPS_Parse2Digit(&dateStr[2], &utcTime->month) == 0U) return 0U;
    if(__DRIVER_GPS_Parse2Digit(&dateStr[4], &yearLow) == 0U) return 0U;

    utcTime->year = (uint16_t)(2000U + yearLow);
    return 1U;
}

/* 解析经纬度字段（ddmm.mmmm / dddmm.mmmm）为十进制度 */
static uint8_t __DRIVER_GPS_ParseCoordinateField(const char *coordStr, char hemisphere, float *degreeValue){
    float rawCoord;
    uint16_t degreePart;
    float minutePart;
    float decimalDegree;

    if((coordStr == NULL) || (degreeValue == NULL)) return 0U;
    if(__DRIVER_GPS_ParseFloat(coordStr, &rawCoord) == 0U) return 0U;

    degreePart = (uint16_t)(rawCoord / 100.0f);
    minutePart = rawCoord - ((float)degreePart * 100.0f);
    decimalDegree = (float)degreePart + (minutePart / 60.0f);

    if((hemisphere == 'S') || (hemisphere == 's') || (hemisphere == 'W') || (hemisphere == 'w')){
        decimalDegree = -decimalDegree;
    }

    *degreeValue = decimalDegree;
    return 1U;
}

/* 按逗号切分NMEA字段（就地切分） */
static uint8_t __DRIVER_GPS_SplitFields(char *payload, char *fields[], uint8_t maxFields){
    uint8_t fieldCount = 0U;
    char *p = payload;

    if((payload == NULL) || (fields == NULL) || (maxFields == 0U)) return 0U;

    fields[fieldCount++] = p;
    while(*p != '\0'){
        if(*p == ','){
            *p = '\0';
            if(fieldCount < maxFields){
                fields[fieldCount++] = p + 1;
            }
        }
        p++;
    }

    return fieldCount;
}

/* 判断语句类型后缀是否匹配（例如GPRMC匹配RMC） */
static uint8_t __DRIVER_GPS_TypeEndsWith(const char *typeStr, const char *suffix){
    size_t typeLen;
    size_t suffixLen;

    if((typeStr == NULL) || (suffix == NULL)) return 0U;

    typeLen = strlen(typeStr);
    suffixLen = strlen(suffix);
    if(typeLen < suffixLen) return 0U;

    if(strcmp(typeStr + (typeLen - suffixLen), suffix) == 0){
        return 1U;
    }
    return 0U;
}

/* 解析RMC语句（时间/经纬度/航向等） */
static void __DRIVER_GPS_ParseRmc(char *fields[], uint8_t fieldCount){
    float value;

    if((fields == NULL) || (fieldCount < 10U)) return;

    gpsInfoCache.fixValid = (fields[2][0] == 'A') ? 1U : 0U;
    if(gpsInfoCache.fixValid == 0U){
        gpsInfoCache.hasLocation = 0U;
    }

    if(__DRIVER_GPS_ParseUtcTimeField(fields[1], &gpsInfoCache.utc) != 0U){
        gpsInfoCache.hasTime = 1U;
    }
    if(__DRIVER_GPS_ParseUtcDateField(fields[9], &gpsInfoCache.utc) != 0U){
        gpsInfoCache.hasTime = 1U;
    }

    if((gpsInfoCache.fixValid != 0U) &&
       (__DRIVER_GPS_ParseCoordinateField(fields[3], fields[4][0], &gpsInfoCache.latitudeDeg) != 0U) &&
       (__DRIVER_GPS_ParseCoordinateField(fields[5], fields[6][0], &gpsInfoCache.longitudeDeg) != 0U)){
        gpsInfoCache.hasLocation = 1U;
    }

    if(__DRIVER_GPS_ParseFloat(fields[7], &value) != 0U){
        gpsInfoCache.speedKnots = value;
        gpsInfoCache.speedKmh = value * 1.852f;
    }

    if(__DRIVER_GPS_ParseFloat(fields[8], &value) != 0U){
        gpsInfoCache.courseDeg = value;
        gpsInfoCache.hasCourse = 1U;
    }

    gpsHasNewDataFlag = 1U;
}

/* 解析VTG语句（优先更新航向/速度） */
static void __DRIVER_GPS_ParseVtg(char *fields[], uint8_t fieldCount){
    float value;

    if((fields == NULL) || (fieldCount < 8U)) return;

    if(__DRIVER_GPS_ParseFloat(fields[1], &value) != 0U){
        gpsInfoCache.courseDeg = value;
        gpsInfoCache.hasCourse = 1U;
    }

    if(__DRIVER_GPS_ParseFloat(fields[5], &value) != 0U){
        gpsInfoCache.speedKnots = value;
    }

    if(__DRIVER_GPS_ParseFloat(fields[7], &value) != 0U){
        gpsInfoCache.speedKmh = value;
    }

    gpsHasNewDataFlag = 1U;
}

/* 解析完整NMEA语句行 */
static void __DRIVER_GPS_ParseNmeaLine(char *line){
    const char *starPos;
    size_t payloadLen;
    char payloadBuf[GPS_NMEA_LINE_MAX_LEN];
    char *fields[GPS_FIELD_MAX];
    uint8_t fieldCount;

    if(line == NULL) return;
    if(__DRIVER_GPS_VerifyNmeaChecksum(line) == 0U) return;

    starPos = strchr(line, '*');
    if(starPos == NULL) return;

    payloadLen = (size_t)(starPos - (line + 1));
    if(payloadLen >= sizeof(payloadBuf)) return;

    memcpy(payloadBuf, line + 1, payloadLen);
    payloadBuf[payloadLen] = '\0';

    fieldCount = __DRIVER_GPS_SplitFields(payloadBuf, fields, GPS_FIELD_MAX);
    if(fieldCount == 0U) return;

    if(__DRIVER_GPS_TypeEndsWith(fields[0], "RMC") != 0U){
        __DRIVER_GPS_ParseRmc(fields, fieldCount);
        return;
    }

    if(__DRIVER_GPS_TypeEndsWith(fields[0], "VTG") != 0U){
        __DRIVER_GPS_ParseVtg(fields, fieldCount);
        return;
    }
}

/* USART3自定义回调：按字节拼接NMEA语句 */
static void __DRIVER_GPS_UartByteCallback(uint8_t port, uint8_t byte){
    if(port != GPS_DEP_UART_PORT) return;

    if(byte == '$'){
        gpsNmeaReceiving = 1U;
        gpsNmeaLineIdx = 0U;
        gpsNmeaLineBuf[gpsNmeaLineIdx++] = '$';
        return;
    }

    if(gpsNmeaReceiving == 0U) return;
    if(byte == '\r') return;

    if(byte == '\n'){
        gpsNmeaLineBuf[gpsNmeaLineIdx] = '\0';
        gpsNmeaReceiving = 0U;
        gpsNmeaLineIdx = 0U;
        __DRIVER_GPS_ParseNmeaLine(gpsNmeaLineBuf);
        return;
    }

    if(gpsNmeaLineIdx >= (GPS_NMEA_LINE_MAX_LEN - 1U)){
        gpsNmeaReceiving = 0U;
        gpsNmeaLineIdx = 0U;
        return;
    }

    gpsNmeaLineBuf[gpsNmeaLineIdx++] = (char)byte;
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化GPS驱动并注册USART3解析回调 */
void DRIVER_GPS_Init(void){
    DRIVER_GPS_Reset();
    GPS_DEP_UART_SET_CUSTOM_CB(__DRIVER_GPS_UartByteCallback);
}

/* 清空缓存与解析状态 */
void DRIVER_GPS_Reset(void){
    memset(&gpsInfoCache, 0, sizeof(gpsInfoCache));
    memset(gpsNmeaLineBuf, 0, sizeof(gpsNmeaLineBuf));
    gpsHasNewDataFlag = 0U;
    gpsNmeaLineIdx = 0U;
    gpsNmeaReceiving = 0U;
}

/* 返回是否存在新解析数据 */
uint8_t DRIVER_GPS_HasNewData(void){
    return gpsHasNewDataFlag;
}

/* 获取GPS完整信息快照 */
uint8_t DRIVER_GPS_GetInfo(gpsInfo_t *info){
    if(info == NULL) return 0U;
    if((gpsInfoCache.hasLocation == 0U) &&
       (gpsInfoCache.hasTime == 0U) &&
       (gpsInfoCache.hasCourse == 0U)){
        return 0U;
    }

    *info = gpsInfoCache;
    gpsHasNewDataFlag = 0U;
    return 1U;
}

/* 获取纬度数据 */
uint8_t DRIVER_GPS_GetLatitudeDeg(float *latitudeDeg){
    if((latitudeDeg == NULL) || (gpsInfoCache.hasLocation == 0U)) return 0U;
    *latitudeDeg = gpsInfoCache.latitudeDeg;
    return 1U;
}

/* 获取经度数据 */
uint8_t DRIVER_GPS_GetLongitudeDeg(float *longitudeDeg){
    if((longitudeDeg == NULL) || (gpsInfoCache.hasLocation == 0U)) return 0U;
    *longitudeDeg = gpsInfoCache.longitudeDeg;
    return 1U;
}

/* 获取UTC时间数据 */
uint8_t DRIVER_GPS_GetUtcTime(gpsUtcTime_t *utcTime){
    if((utcTime == NULL) || (gpsInfoCache.hasTime == 0U)) return 0U;
    *utcTime = gpsInfoCache.utc;
    return 1U;
}

/* 获取航向角数据 */
uint8_t DRIVER_GPS_GetCourseDeg(float *courseDeg){
    if((courseDeg == NULL) || (gpsInfoCache.hasCourse == 0U)) return 0U;
    *courseDeg = gpsInfoCache.courseDeg;
    return 1U;
}
