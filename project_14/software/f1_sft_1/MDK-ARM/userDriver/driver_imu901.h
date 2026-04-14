#ifndef __DRIVER_IMU901_H__
#define __DRIVER_IMU901_H__

#include <stdint.h>
#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 * 依赖：stdlib_usart
 *============================================================================*/

/* IMU901使用的串口端口（USART3） */
#define IMU901_DEP_UART_PORT    UART_PORT2

/*============================================================================
 * 向上提供
 *============================================================================*/

/* ATK-MS901M十轴IMU模块信息结构体 */
typedef struct {
  /* 姿态角（单位：°） */
  float roll;
  float pitch;
  float yaw;

  /* 四元数（无量纲） */
  float q0;
  float q1;
  float q2;
  float q3;

  /* 加速度计（单位：G，默认满量程 4G） */
  float accX;
  float accY;
  float accZ;

  /* 陀螺仪（单位：°/s，默认满量程 2000dps） */
  float gyroX;
  float gyroY;
  float gyroZ;

  /* 磁力计（原始ADC值，无量纲） */
  int16_t magX;
  int16_t magY;
  int16_t magZ;
  float   magTemp;   /* 磁力计板载温度（单位：℃） */

  /* 气压计 */
  int32_t pressure;  /* 气压（单位：Pa） */
  int32_t altitude;  /* 海拔高度（单位：cm） */
  float   baroTemp;  /* 气压计板载温度（单位：℃） */

  /* 扩展端口状态（D0~D3，含义由端口模式决定） */
  uint16_t portD[4];
} imu901Info_t;

/* IMU901模块公共数据，供上层访问 */
extern imu901Info_t imu901Info;

/* 初始化IMU901驱动，向stdlib_usart注册串口3自定义字节回调 */
void DRIVER_IMU901_Init(void);

/* 更新IMU901数据，在task层10ms周期调用 */
void DRIVER_IMU901_Update(void);

#endif
