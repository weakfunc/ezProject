#ifndef __DRIVER_MPU6050_H__
#define __DRIVER_MPU6050_H__

#include "stdlib_i2c.h"
#include "stdlib_common.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要，依赖 stdlib_i2c、stdlib_common）
 *============================================================================*/
/* [MODIFIED] MPU6050模块接入I2C_2：PA12=SDA，PA11=SCL */
#define MPU6050_DEP_I2C_BUS                    I2C_BUS_2
#define MPU6050_DEP_I2C_WRITE_BYTE(reg, val)   STDLIB_I2C_WriteByte(MPU6050_DEP_I2C_BUS, MPU6050_I2C_ADDR_WRITE, (reg), (val))
#define MPU6050_DEP_I2C_READ_DATA(reg, buf, n) STDLIB_I2C_ReadData(MPU6050_DEP_I2C_BUS, MPU6050_I2C_ADDR_WRITE, (reg), (buf), (n))
#define MPU6050_DEP_TICK_MS()                  STDLIB_COMMON_GetTickMs()

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/
/* MPU-6050 7位I2C地址（AD0接低电平） */
#define MPU6050_I2C_ADDR_7BIT                  (0x68U)
/* 含写位的8位I2C地址 */
#define MPU6050_I2C_ADDR_WRITE                 ((uint8_t)(MPU6050_I2C_ADDR_7BIT << 1U))

/* 加速度计量程 ±2g，对应灵敏度 16384 LSB/g */
#define MPU6050_ACCEL_FSR_G                    (2.0f)
/* 陀螺仪量程 ±250°/s，对应灵敏度 131 LSB/(°/s) */
#define MPU6050_GYRO_FSR_DPS                   (250.0f)

/* 加速度计数据（三轴线加速度） */
typedef struct {
    int16_t rawX;  /* X轴原始值 */
    int16_t rawY;  /* Y轴原始值 */
    int16_t rawZ;  /* Z轴原始值 */
    float xG;      /* X轴加速度（g） */
    float yG;      /* Y轴加速度（g） */
    float zG;      /* Z轴加速度（g） */
} mpu6050AccelData_t;

/* 陀螺仪数据（三轴角速度） */
typedef struct {
    int16_t rawX;  /* X轴原始值 */
    int16_t rawY;  /* Y轴原始值 */
    int16_t rawZ;  /* Z轴原始值 */
    float xDps;    /* X轴角速度（°/s） */
    float yDps;    /* Y轴角速度（°/s） */
    float zDps;    /* Z轴角速度（°/s） */
} mpu6050GyroData_t;

/* 三轴融合角度数据（互补滤波输出） */
typedef struct {
    float rollDeg;   /* 横滚角（绕X轴，单位：°） */
    float pitchDeg;  /* 俯仰角（绕Y轴，单位：°） */
    float yawDeg;    /* 偏航角（绕Z轴，单位：°，仅陀螺积分，存在漂移） */
} mpu6050AngleData_t;

/* MPU6050模块信息（公有管理结构体） */
typedef struct {
    mpu6050AccelData_t  accel;  /* 加速度计数据 */
    mpu6050GyroData_t   gyro;   /* 陀螺仪数据 */
    mpu6050AngleData_t  angle;  /* 融合角度数据 */
    uint8_t isReady;            /* 数据就绪标志（1=已完成首次读取，0=未就绪） */
} mpu6050Info_t;

extern mpu6050Info_t mpu6050Info;

/* 初始化MPU6050，唤醒芯片并配置量程 */
void DRIVER_MPU6050_Init(void);

/* 从MPU6050读取一帧原始数据并更新mpu6050Info，需周期性调用 */
void DRIVER_MPU6050_Update(void);

/* 获取陀螺仪原始传感器读值（含加速度计与陀螺仪原始int16值） */
uint8_t DRIVER_MPU6050_GetRawData(mpu6050AccelData_t *accel, mpu6050GyroData_t *gyro);

/* 获取三轴角速度（单位：°/s） */
uint8_t DRIVER_MPU6050_GetGyro(mpu6050GyroData_t *gyro);

/* 获取三轴线加速度（单位：g） */
uint8_t DRIVER_MPU6050_GetAccel(mpu6050AccelData_t *accel);

/* 获取三轴融合角度（roll/pitch为互补滤波，yaw为陀螺积分，单位：°） */
uint8_t DRIVER_MPU6050_GetAngle(mpu6050AngleData_t *angle);

#endif
