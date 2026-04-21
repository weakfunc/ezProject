/*============================================================================
 * README
 * [MODIFIED] MPU6050模块改为通过I2C_BUS_2读取三轴加速度和三轴角速度原始数据
 * 并转换为物理量。需在任务层周期性调用 DRIVER_MPU6050_Update()。
 *============================================================================*/

#include "driver_mpu6050.h"
#include <string.h>
#include <math.h>

/*============================================================================
 * 内部配置（仅driver_mpu6050模块内部使用）
 *============================================================================*/

/* MPU-6050寄存器地址 */
#define MPU6050_REG_SMPLRT_DIV     (0x19U)  /* 采样率分频寄存器 */
#define MPU6050_REG_CONFIG         (0x1AU)  /* 配置寄存器（DLPF） */
#define MPU6050_REG_GYRO_CONFIG    (0x1BU)  /* 陀螺仪量程配置 */
#define MPU6050_REG_ACCEL_CONFIG   (0x1CU)  /* 加速度计量程配置 */
#define MPU6050_REG_ACCEL_XOUT_H   (0x3BU)  /* 加速度计X轴高字节，连续14字节含陀螺仪 */
#define MPU6050_REG_PWR_MGMT_1     (0x6BU)  /* 电源管理寄存器1 */

/* 原始数据缓冲区长度：加速度计（6字节）+ 温度（2字节）+ 陀螺仪（6字节）= 14字节 */
#define MPU6050_RAW_BUF_LEN        (14U)

/* 陀螺仪灵敏度倒数（±250°/s量程，131 LSB/(°/s)） */
#define MPU6050_GYRO_SCALE         (MPU6050_GYRO_FSR_DPS / 32768.0f)

/* 加速度计灵敏度倒数（±2g量程，16384 LSB/g） */
#define MPU6050_ACCEL_SCALE        (MPU6050_ACCEL_FSR_G / 32768.0f)

/* 互补滤波系数（陀螺仪权重0.98，加速度计权重0.02） */
#define MPU6050_COMP_ALPHA         (0.98f)

/* 弧度转角度系数 */
#define MPU6050_RAD_TO_DEG         (57.29577951f)

/* MPU6050最新数据缓存 */
mpu6050Info_t mpu6050Info;

/* 上次Update时的毫秒时间戳，用于计算dt */
static uint32_t mpu6050LastTickMs = 0U;
/* 角度滤波是否已完成首次初始化 */
static uint8_t mpu6050AngleInited = 0U;

/* 将两个字节合并为有符号16位整数（大端序） */
static int16_t __DRIVER_MPU6050_CombineBytes(uint8_t highByte, uint8_t lowByte){
    return (int16_t)(((uint16_t)highByte << 8U) | (uint16_t)lowByte);
}

/* 将14字节原始缓冲区解析并写入mpu6050Info */
static void __DRIVER_MPU6050_ParseRawBuf(const uint8_t *buf){
    /* 加速度计：buf[0..5] */
    mpu6050Info.accel.rawX = __DRIVER_MPU6050_CombineBytes(buf[0], buf[1]);
    mpu6050Info.accel.rawY = __DRIVER_MPU6050_CombineBytes(buf[2], buf[3]);
    mpu6050Info.accel.rawZ = __DRIVER_MPU6050_CombineBytes(buf[4], buf[5]);

    /* buf[6..7] 为温度，忽略 */

    /* 陀螺仪：buf[8..13] */
    mpu6050Info.gyro.rawX = __DRIVER_MPU6050_CombineBytes(buf[8],  buf[9]);
    mpu6050Info.gyro.rawY = __DRIVER_MPU6050_CombineBytes(buf[10], buf[11]);
    mpu6050Info.gyro.rawZ = __DRIVER_MPU6050_CombineBytes(buf[12], buf[13]);

    /* 转换为物理量 */
    mpu6050Info.accel.xG  = (float)mpu6050Info.accel.rawX * MPU6050_ACCEL_SCALE;
    mpu6050Info.accel.yG  = (float)mpu6050Info.accel.rawY * MPU6050_ACCEL_SCALE;
    mpu6050Info.accel.zG  = (float)mpu6050Info.accel.rawZ * MPU6050_ACCEL_SCALE;

    mpu6050Info.gyro.xDps = (float)mpu6050Info.gyro.rawX * MPU6050_GYRO_SCALE;
    mpu6050Info.gyro.yDps = (float)mpu6050Info.gyro.rawY * MPU6050_GYRO_SCALE;
    mpu6050Info.gyro.zDps = (float)mpu6050Info.gyro.rawZ * MPU6050_GYRO_SCALE;

    mpu6050Info.isReady = 1U;
}

/* 根据当前加速度计和陀螺仪数据更新互补滤波角度 */
static void __DRIVER_MPU6050_UpdateAngle(void){
    float accelRoll;
    float accelPitch;
    float dt;
    uint32_t nowMs;

    nowMs = MPU6050_DEP_TICK_MS();

    /* 由加速度计计算参考roll和pitch（弧度→角度） */
    accelRoll  = atan2f(mpu6050Info.accel.yG, mpu6050Info.accel.zG) * MPU6050_RAD_TO_DEG;
    accelPitch = atan2f(-mpu6050Info.accel.xG,
                        sqrtf(mpu6050Info.accel.yG * mpu6050Info.accel.yG +
                              mpu6050Info.accel.zG * mpu6050Info.accel.zG)) * MPU6050_RAD_TO_DEG;

    if(mpu6050AngleInited == 0U){
        /* 首次调用直接以加速度计角度初始化，yaw归零 */
        mpu6050Info.angle.rollDeg  = accelRoll;
        mpu6050Info.angle.pitchDeg = accelPitch;
        mpu6050Info.angle.yawDeg   = 0.0f;
        mpu6050AngleInited = 1U;
    } else {
        dt = (float)(nowMs - mpu6050LastTickMs) * 0.001f;

        /* 互补滤波：陀螺仪积分 + 加速度计修正 */
        mpu6050Info.angle.rollDeg  = MPU6050_COMP_ALPHA * (mpu6050Info.angle.rollDeg  + mpu6050Info.gyro.xDps * dt)
                                   + (1.0f - MPU6050_COMP_ALPHA) * accelRoll;
        mpu6050Info.angle.pitchDeg = MPU6050_COMP_ALPHA * (mpu6050Info.angle.pitchDeg + mpu6050Info.gyro.yDps * dt)
                                   + (1.0f - MPU6050_COMP_ALPHA) * accelPitch;

        /* yaw仅靠陀螺仪积分，无加速度计修正 */
        mpu6050Info.angle.yawDeg  += mpu6050Info.gyro.zDps * dt;
    }

    mpu6050LastTickMs = nowMs;
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化MPU6050：唤醒芯片，配置采样率、DLPF、量程 */
void DRIVER_MPU6050_Init(void){
    memset(&mpu6050Info, 0, sizeof(mpu6050Info));
    mpu6050LastTickMs  = 0U;
    mpu6050AngleInited = 0U;

    /* 退出睡眠模式，使用内部8MHz振荡器 */
    MPU6050_DEP_I2C_WRITE_BYTE(MPU6050_REG_PWR_MGMT_1, 0x00U);

    /* 采样率分频：SMPLRT_DIV=7 → 采样率=1kHz/(7+1)=125Hz */
    MPU6050_DEP_I2C_WRITE_BYTE(MPU6050_REG_SMPLRT_DIV, 0x07U);

    /* 数字低通滤波器：带宽约5Hz */
    MPU6050_DEP_I2C_WRITE_BYTE(MPU6050_REG_CONFIG, 0x06U);

    /* 陀螺仪量程：±250°/s */
    MPU6050_DEP_I2C_WRITE_BYTE(MPU6050_REG_GYRO_CONFIG, 0x00U);

    /* 加速度计量程：±2g */
    MPU6050_DEP_I2C_WRITE_BYTE(MPU6050_REG_ACCEL_CONFIG, 0x00U);
}

/* 从MPU6050连续读取14字节原始数据，更新mpu6050Info并计算融合角度 */
void DRIVER_MPU6050_Update(void){
    uint8_t buf[MPU6050_RAW_BUF_LEN];

    MPU6050_DEP_I2C_READ_DATA(MPU6050_REG_ACCEL_XOUT_H, buf, MPU6050_RAW_BUF_LEN);
    __DRIVER_MPU6050_ParseRawBuf(buf);
    __DRIVER_MPU6050_UpdateAngle();
}

/* 获取陀螺仪原始传感器读值（含加速度计与陀螺仪原始int16值） */
uint8_t DRIVER_MPU6050_GetRawData(mpu6050AccelData_t *accel, mpu6050GyroData_t *gyro){
    if(mpu6050Info.isReady == 0U) return 0U;
    if(accel != NULL){
        *accel = mpu6050Info.accel;
    }
    if(gyro != NULL){
        *gyro = mpu6050Info.gyro;
    }
    return 1U;
}

/* 获取三轴角速度（单位：°/s） */
uint8_t DRIVER_MPU6050_GetGyro(mpu6050GyroData_t *gyro){
    if((gyro == NULL) || (mpu6050Info.isReady == 0U)) return 0U;
    *gyro = mpu6050Info.gyro;
    return 1U;
}

/* 获取三轴线加速度（单位：g） */
uint8_t DRIVER_MPU6050_GetAccel(mpu6050AccelData_t *accel){
    if((accel == NULL) || (mpu6050Info.isReady == 0U)) return 0U;
    *accel = mpu6050Info.accel;
    return 1U;
}

/* 获取三轴融合角度（roll/pitch为互补滤波，yaw为陀螺积分，单位：°） */
uint8_t DRIVER_MPU6050_GetAngle(mpu6050AngleData_t *angle){
    if((angle == NULL) || (mpu6050Info.isReady == 0U)) return 0U;
    *angle = mpu6050Info.angle;
    return 1U;
}
