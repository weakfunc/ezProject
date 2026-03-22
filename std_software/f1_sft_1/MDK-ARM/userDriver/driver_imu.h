#ifndef __DRIVER_IMU_H__
#define __DRIVER_IMU_H__

#include "stdlib_common.h"
#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 *============================================================================*/
#define IMU_DEP_UART_PORT                      UART_PORT3
#define IMU_DEP_UART_SET_CUSTOM_CB(cb)         STDLIB_USART_SetCustomCb(IMU_DEP_UART_PORT, (cb))
#define IMU_DEP_TICK_MS()                      STDLIB_COMMON_GetTickMs()

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/
#define IMU_FRAME_HEAD                         (0x55U)
#define IMU_FRAME_DATA_MAX_LEN                 (12U)

/* 按模块默认配置换算物理量 */
#define IMU_CFG_GYRO_FSR_DPS                   (2000.0f)
#define IMU_CFG_ACC_FSR_G                      (4.0f)

/* 数据有效位与更新位共用同一组标志 */
#define IMU_DATA_FLAG_ANGLE                    (1UL << 0)
#define IMU_DATA_FLAG_QUATERNION               (1UL << 1)
#define IMU_DATA_FLAG_ACCEL_GYRO               (1UL << 2)
#define IMU_DATA_FLAG_MAG                      (1UL << 3)
#define IMU_DATA_FLAG_ALTIMETER                (1UL << 4)

/* 姿态角数据 */
typedef struct {
    int16_t rawRoll;
    int16_t rawPitch;
    int16_t rawYaw;
    float rollDeg;
    float pitchDeg;
    float yawDeg;
} imuAngleData_t;

/* 四元数数据 */
typedef struct {
    int16_t rawQ0;
    int16_t rawQ1;
    int16_t rawQ2;
    int16_t rawQ3;
    float q0;
    float q1;
    float q2;
    float q3;
} imuQuaternionData_t;

/* 加速度计数据 */
typedef struct {
    int16_t rawX;
    int16_t rawY;
    int16_t rawZ;
    float xG;
    float yG;
    float zG;
} imuAccelData_t;

/* 陀螺仪数据 */
typedef struct {
    int16_t rawX;
    int16_t rawY;
    int16_t rawZ;
    float xDps;
    float yDps;
    float zDps;
} imuGyroData_t;

/* 磁力计数据 */
typedef struct {
    int16_t rawX;
    int16_t rawY;
    int16_t rawZ;
    int16_t rawTemp;
    float temperatureDegC;
} imuMagData_t;

/* 高度计/气压计数据 */
typedef struct {
    int32_t pressurePa;
    int32_t altitudeCm;
    int16_t rawTemp;
    float temperatureDegC;
} imuAltimeterData_t;

/* IMU完整信息快照 */
typedef struct {
    imuAngleData_t angle;
    imuQuaternionData_t quaternion;
    imuAccelData_t accel;
    imuGyroData_t gyro;
    imuMagData_t mag;
    imuAltimeterData_t altimeter;
    uint32_t validMask;
    uint32_t updateMask;
    uint32_t lastUpdateTickMs;
} imuInfo_t;

/* 初始化IMU驱动并注册USART3字节回调 */
void DRIVER_IMU_Init(void);

/* 清空IMU缓存与协议解析状态 */
void DRIVER_IMU_Reset(void);

/* 查询是否有新的IMU数据更新 */
uint8_t DRIVER_IMU_HasNewData(void);

/* 获取当前累计的更新标志位 */
uint32_t DRIVER_IMU_GetUpdateMask(void);

/* 获取最新IMU完整信息快照，并清除更新标志 */
uint8_t DRIVER_IMU_GetInfo(imuInfo_t *info);

/* 获取姿态角数据 */
uint8_t DRIVER_IMU_GetAngle(imuAngleData_t *angle);

/* 获取陀螺仪角速度数据 */
uint8_t DRIVER_IMU_GetGyro(imuGyroData_t *gyro);

/* 获取加速度计数据（任务描述中的“角加速度”对应加速度计输出） */
uint8_t DRIVER_IMU_GetAccel(imuAccelData_t *accel);

/* 获取高度计/气压计数据 */
uint8_t DRIVER_IMU_GetAltimeter(imuAltimeterData_t *altimeter);

/* 获取四元数数据 */
uint8_t DRIVER_IMU_GetQuaternion(imuQuaternionData_t *quaternion);

#endif
