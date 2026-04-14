/*============================================================================
 * README
 *
 *============================================================================*/

#include "driver_imu.h"
#include <string.h>

/*============================================================================
 * 内部配置（仅driver_imu模块内部使用）
 *============================================================================*/

#define IMU_FRAME_ID_ANGLE                    (0x01U)
#define IMU_FRAME_ID_QUATERNION               (0x02U)
#define IMU_FRAME_ID_ACCEL_GYRO               (0x03U)
#define IMU_FRAME_ID_MAG                      (0x04U)
#define IMU_FRAME_ID_ALTIMETER                (0x05U)

#define IMU_ANGLE_SCALE_DEG                   (180.0f / 32768.0f)
#define IMU_QUATERNION_SCALE                  (1.0f / 32768.0f)

/* IMU字节流解析状态 */
typedef enum {
    IMU_PARSE_WAIT_HEAD1 = 0U,
    IMU_PARSE_WAIT_HEAD2,
    IMU_PARSE_WAIT_ID,
    IMU_PARSE_WAIT_LEN,
    IMU_PARSE_WAIT_DATA,
    IMU_PARSE_WAIT_SUM,
} imuParseState_t;

/* IMU协议解析上下文 */
typedef struct {
    imuParseState_t state;
    uint8_t frameId;
    uint8_t dataLen;
    uint8_t dataIdx;
    uint8_t checksum;
    uint8_t data[IMU_FRAME_DATA_MAX_LEN];
} imuParseCtx_t;

/* IMU最新数据缓存 */
imuInfo_t imuInfo;
/* IMU协议解析状态 */
static imuParseCtx_t imuParseCtx;

/* 进入临界区，避免任务读取时被串口中断打断 */
static uint32_t __DRIVER_IMU_EnterCritical(void){
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

/* 退出临界区，恢复进入前的中断状态 */
static void __DRIVER_IMU_ExitCritical(uint32_t primask){
    if(primask == 0U){
        __enable_irq();
    }
}

/* 将协议解析器恢复到等待帧头状态 */
static void __DRIVER_IMU_ResetParser(void){
    imuParseCtx.state = IMU_PARSE_WAIT_HEAD1;
    imuParseCtx.frameId = 0U;
    imuParseCtx.dataLen = 0U;
    imuParseCtx.dataIdx = 0U;
    imuParseCtx.checksum = 0U;
    memset(imuParseCtx.data, 0, sizeof(imuParseCtx.data));
}

/* 当当前字节可能是新帧帧头时，直接从该字节重新同步 */
static void __DRIVER_IMU_RestartParserFromByte(uint8_t byte){
    __DRIVER_IMU_ResetParser();
    if(byte == IMU_FRAME_HEAD){
        imuParseCtx.state = IMU_PARSE_WAIT_HEAD2;
        imuParseCtx.checksum = IMU_FRAME_HEAD;
    }
}

/* 解析小端16位有符号整数 */
static int16_t __DRIVER_IMU_ParseInt16(uint8_t lowByte, uint8_t highByte){
    return (int16_t)(((uint16_t)highByte << 8U) | (uint16_t)lowByte);
}

/* 解析小端32位有符号整数 */
static int32_t __DRIVER_IMU_ParseInt32(uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3){
    uint32_t value;

    value = ((uint32_t)byte3 << 24U) |
            ((uint32_t)byte2 << 16U) |
            ((uint32_t)byte1 << 8U) |
            (uint32_t)byte0;

    return (int32_t)value;
}

/* 将最新帧对应的数据类型标记为有效并更新 */
static void __DRIVER_IMU_MarkUpdated(uint32_t dataFlag){
    imuInfo.validMask |= dataFlag;
    imuInfo.updateMask |= dataFlag;
    imuInfo.lastUpdateTickMs = IMU_DEP_TICK_MS();
}

/* 解析姿态角数据帧 */
static void __DRIVER_IMU_ParseAngleFrame(const uint8_t *data){
    imuInfo.angle.rawRoll = __DRIVER_IMU_ParseInt16(data[0], data[1]);
    imuInfo.angle.rawPitch = __DRIVER_IMU_ParseInt16(data[2], data[3]);
    imuInfo.angle.rawYaw = __DRIVER_IMU_ParseInt16(data[4], data[5]);

    imuInfo.angle.rollDeg = (float)imuInfo.angle.rawRoll * IMU_ANGLE_SCALE_DEG;
    imuInfo.angle.pitchDeg = (float)imuInfo.angle.rawPitch * IMU_ANGLE_SCALE_DEG;
    imuInfo.angle.yawDeg = (float)imuInfo.angle.rawYaw * IMU_ANGLE_SCALE_DEG;

    __DRIVER_IMU_MarkUpdated(IMU_DATA_FLAG_ANGLE);
}

/* 解析四元数数据帧 */
static void __DRIVER_IMU_ParseQuaternionFrame(const uint8_t *data){
    imuInfo.quaternion.rawQ0 = __DRIVER_IMU_ParseInt16(data[0], data[1]);
    imuInfo.quaternion.rawQ1 = __DRIVER_IMU_ParseInt16(data[2], data[3]);
    imuInfo.quaternion.rawQ2 = __DRIVER_IMU_ParseInt16(data[4], data[5]);
    imuInfo.quaternion.rawQ3 = __DRIVER_IMU_ParseInt16(data[6], data[7]);

    imuInfo.quaternion.q0 = (float)imuInfo.quaternion.rawQ0 * IMU_QUATERNION_SCALE;
    imuInfo.quaternion.q1 = (float)imuInfo.quaternion.rawQ1 * IMU_QUATERNION_SCALE;
    imuInfo.quaternion.q2 = (float)imuInfo.quaternion.rawQ2 * IMU_QUATERNION_SCALE;
    imuInfo.quaternion.q3 = (float)imuInfo.quaternion.rawQ3 * IMU_QUATERNION_SCALE;

    __DRIVER_IMU_MarkUpdated(IMU_DATA_FLAG_QUATERNION);
}

/* 解析加速度计与陀螺仪数据帧 */
static void __DRIVER_IMU_ParseAccelGyroFrame(const uint8_t *data){
    imuInfo.accel.rawX = __DRIVER_IMU_ParseInt16(data[0], data[1]);
    imuInfo.accel.rawY = __DRIVER_IMU_ParseInt16(data[2], data[3]);
    imuInfo.accel.rawZ = __DRIVER_IMU_ParseInt16(data[4], data[5]);
    imuInfo.gyro.rawX = __DRIVER_IMU_ParseInt16(data[6], data[7]);
    imuInfo.gyro.rawY = __DRIVER_IMU_ParseInt16(data[8], data[9]);
    imuInfo.gyro.rawZ = __DRIVER_IMU_ParseInt16(data[10], data[11]);

    imuInfo.accel.xG = ((float)imuInfo.accel.rawX / 32768.0f) * IMU_CFG_ACC_FSR_G;
    imuInfo.accel.yG = ((float)imuInfo.accel.rawY / 32768.0f) * IMU_CFG_ACC_FSR_G;
    imuInfo.accel.zG = ((float)imuInfo.accel.rawZ / 32768.0f) * IMU_CFG_ACC_FSR_G;

    imuInfo.gyro.xDps = ((float)imuInfo.gyro.rawX / 32768.0f) * IMU_CFG_GYRO_FSR_DPS;
    imuInfo.gyro.yDps = ((float)imuInfo.gyro.rawY / 32768.0f) * IMU_CFG_GYRO_FSR_DPS;
    imuInfo.gyro.zDps = ((float)imuInfo.gyro.rawZ / 32768.0f) * IMU_CFG_GYRO_FSR_DPS;

    __DRIVER_IMU_MarkUpdated(IMU_DATA_FLAG_ACCEL_GYRO);
}

/* 解析磁力计数据帧 */
static void __DRIVER_IMU_ParseMagFrame(const uint8_t *data){
    imuInfo.mag.rawX = __DRIVER_IMU_ParseInt16(data[0], data[1]);
    imuInfo.mag.rawY = __DRIVER_IMU_ParseInt16(data[2], data[3]);
    imuInfo.mag.rawZ = __DRIVER_IMU_ParseInt16(data[4], data[5]);
    imuInfo.mag.rawTemp = __DRIVER_IMU_ParseInt16(data[6], data[7]);
    imuInfo.mag.temperatureDegC = (float)imuInfo.mag.rawTemp / 100.0f;

    __DRIVER_IMU_MarkUpdated(IMU_DATA_FLAG_MAG);
}

/* 解析高度计/气压计数据帧 */
static void __DRIVER_IMU_ParseAltimeterFrame(const uint8_t *data){
    imuInfo.altimeter.pressurePa = __DRIVER_IMU_ParseInt32(data[0], data[1], data[2], data[3]);
    imuInfo.altimeter.altitudeCm = __DRIVER_IMU_ParseInt32(data[4], data[5], data[6], data[7]);
    imuInfo.altimeter.rawTemp = __DRIVER_IMU_ParseInt16(data[8], data[9]);
    imuInfo.altimeter.temperatureDegC = (float)imuInfo.altimeter.rawTemp / 100.0f;

    __DRIVER_IMU_MarkUpdated(IMU_DATA_FLAG_ALTIMETER);
}

/* 按帧ID分发并解析已通过校验的数据帧 */
static void __DRIVER_IMU_ParseFrame(uint8_t frameId, const uint8_t *data, uint8_t dataLen){
    switch(frameId){
        case IMU_FRAME_ID_ANGLE:
            if(dataLen == 6U){
                __DRIVER_IMU_ParseAngleFrame(data);
            }
            break;

        case IMU_FRAME_ID_QUATERNION:
            if(dataLen == 8U){
                __DRIVER_IMU_ParseQuaternionFrame(data);
            }
            break;

        case IMU_FRAME_ID_ACCEL_GYRO:
            if(dataLen == 12U){
                __DRIVER_IMU_ParseAccelGyroFrame(data);
            }
            break;

        case IMU_FRAME_ID_MAG:
            if(dataLen == 8U){
                __DRIVER_IMU_ParseMagFrame(data);
            }
            break;

        case IMU_FRAME_ID_ALTIMETER:
            if(dataLen == 10U){
                __DRIVER_IMU_ParseAltimeterFrame(data);
            }
            break;

        default:
            break;
    }
}

/* USART3自定义回调：按字节解析IMU主动上报帧 */
static void __DRIVER_IMU_UartByteCallback(uint8_t port, uint8_t byte){
    if(port != IMU_DEP_UART_PORT) return;

    switch(imuParseCtx.state){
        case IMU_PARSE_WAIT_HEAD1:
            if(byte == IMU_FRAME_HEAD){
                imuParseCtx.state = IMU_PARSE_WAIT_HEAD2;
                imuParseCtx.checksum = IMU_FRAME_HEAD;
            }
            break;

        case IMU_PARSE_WAIT_HEAD2:
            if(byte == IMU_FRAME_HEAD){
                imuParseCtx.state = IMU_PARSE_WAIT_ID;
                imuParseCtx.checksum = (uint8_t)(imuParseCtx.checksum + byte);
            } else {
                __DRIVER_IMU_RestartParserFromByte(byte);
            }
            break;

        case IMU_PARSE_WAIT_ID:
            imuParseCtx.frameId = byte;
            imuParseCtx.checksum = (uint8_t)(imuParseCtx.checksum + byte);
            imuParseCtx.state = IMU_PARSE_WAIT_LEN;
            break;

        case IMU_PARSE_WAIT_LEN:
            if((byte == 0U) || (byte > IMU_FRAME_DATA_MAX_LEN)){
                __DRIVER_IMU_RestartParserFromByte(byte);
                break;
            }
            imuParseCtx.dataLen = byte;
            imuParseCtx.dataIdx = 0U;
            imuParseCtx.checksum = (uint8_t)(imuParseCtx.checksum + byte);
            imuParseCtx.state = IMU_PARSE_WAIT_DATA;
            break;

        case IMU_PARSE_WAIT_DATA:
            imuParseCtx.data[imuParseCtx.dataIdx++] = byte;
            imuParseCtx.checksum = (uint8_t)(imuParseCtx.checksum + byte);
            if(imuParseCtx.dataIdx >= imuParseCtx.dataLen){
                imuParseCtx.state = IMU_PARSE_WAIT_SUM;
            }
            break;

        case IMU_PARSE_WAIT_SUM:
            if(byte == imuParseCtx.checksum){
                __DRIVER_IMU_ParseFrame(imuParseCtx.frameId, imuParseCtx.data, imuParseCtx.dataLen);
                __DRIVER_IMU_ResetParser();
            } else {
                __DRIVER_IMU_RestartParserFromByte(byte);
            }
            break;

        default:
            __DRIVER_IMU_ResetParser();
            break;
    }
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化IMU驱动并注册USART3协议解析回调 */
void DRIVER_IMU_Init(void){
    DRIVER_IMU_Reset();
    IMU_DEP_UART_SET_CUSTOM_CB(__DRIVER_IMU_UartByteCallback);
}

/* 清空缓存与字节流解析状态 */
void DRIVER_IMU_Reset(void){
    uint32_t primask = __DRIVER_IMU_EnterCritical();

    memset(&imuInfo, 0, sizeof(imuInfo));
    __DRIVER_IMU_ResetParser();

    __DRIVER_IMU_ExitCritical(primask);
}

/* 查询是否存在新的IMU数据更新 */
uint8_t DRIVER_IMU_HasNewData(void){
    uint8_t hasNewData;
    uint32_t primask = __DRIVER_IMU_EnterCritical();

    hasNewData = (imuInfo.updateMask != 0U) ? 1U : 0U;

    __DRIVER_IMU_ExitCritical(primask);
    return hasNewData;
}

/* 获取当前累计的更新标志位 */
uint32_t DRIVER_IMU_GetUpdateMask(void){
    uint32_t updateMask;
    uint32_t primask = __DRIVER_IMU_EnterCritical();

    updateMask = imuInfo.updateMask;

    __DRIVER_IMU_ExitCritical(primask);
    return updateMask;
}

/* 获取IMU完整快照，并清除累计更新标志 */
uint8_t DRIVER_IMU_GetInfo(imuInfo_t *info){
    uint8_t hasValidData;
    uint32_t primask;

    if(info == NULL) return 0U;

    primask = __DRIVER_IMU_EnterCritical();
    hasValidData = (imuInfo.validMask != 0U) ? 1U : 0U;
    if(hasValidData != 0U){
        *info = imuInfo;
        imuInfo.updateMask = 0U;
    }
    __DRIVER_IMU_ExitCritical(primask);

    return hasValidData;
}

/* 获取姿态角数据 */
uint8_t DRIVER_IMU_GetAngle(imuAngleData_t *angle){
    uint8_t hasAngle;
    uint32_t primask;

    if(angle == NULL) return 0U;

    primask = __DRIVER_IMU_EnterCritical();
    hasAngle = ((imuInfo.validMask & IMU_DATA_FLAG_ANGLE) != 0U) ? 1U : 0U;
    if(hasAngle != 0U){
        *angle = imuInfo.angle;
    }
    __DRIVER_IMU_ExitCritical(primask);

    return hasAngle;
}

/* 获取陀螺仪角速度数据 */
uint8_t DRIVER_IMU_GetGyro(imuGyroData_t *gyro){
    uint8_t hasGyro;
    uint32_t primask;

    if(gyro == NULL) return 0U;

    primask = __DRIVER_IMU_EnterCritical();
    hasGyro = ((imuInfo.validMask & IMU_DATA_FLAG_ACCEL_GYRO) != 0U) ? 1U : 0U;
    if(hasGyro != 0U){
        *gyro = imuInfo.gyro;
    }
    __DRIVER_IMU_ExitCritical(primask);

    return hasGyro;
}

/* 获取加速度计数据 */
uint8_t DRIVER_IMU_GetAccel(imuAccelData_t *accel){
    uint8_t hasAccel;
    uint32_t primask;

    if(accel == NULL) return 0U;

    primask = __DRIVER_IMU_EnterCritical();
    hasAccel = ((imuInfo.validMask & IMU_DATA_FLAG_ACCEL_GYRO) != 0U) ? 1U : 0U;
    if(hasAccel != 0U){
        *accel = imuInfo.accel;
    }
    __DRIVER_IMU_ExitCritical(primask);

    return hasAccel;
}

/* 获取高度计/气压计数据 */
uint8_t DRIVER_IMU_GetAltimeter(imuAltimeterData_t *altimeter){
    uint8_t hasAltimeter;
    uint32_t primask;

    if(altimeter == NULL) return 0U;

    primask = __DRIVER_IMU_EnterCritical();
    hasAltimeter = ((imuInfo.validMask & IMU_DATA_FLAG_ALTIMETER) != 0U) ? 1U : 0U;
    if(hasAltimeter != 0U){
        *altimeter = imuInfo.altimeter;
    }
    __DRIVER_IMU_ExitCritical(primask);

    return hasAltimeter;
}

/* 获取四元数数据 */
uint8_t DRIVER_IMU_GetQuaternion(imuQuaternionData_t *quaternion){
    uint8_t hasQuaternion;
    uint32_t primask;

    if(quaternion == NULL) return 0U;

    primask = __DRIVER_IMU_EnterCritical();
    hasQuaternion = ((imuInfo.validMask & IMU_DATA_FLAG_QUATERNION) != 0U) ? 1U : 0U;
    if(hasQuaternion != 0U){
        *quaternion = imuInfo.quaternion;
    }
    __DRIVER_IMU_ExitCritical(primask);

    return hasQuaternion;
}
