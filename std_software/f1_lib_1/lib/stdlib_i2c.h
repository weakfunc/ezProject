#ifndef __STDLIB_I2C_H__
#define __STDLIB_I2C_H__
#include "stdlib_config.h"
#include "stdlib_common.h"

/*============================================================================
 * 内部配置
 *============================================================================*/

/* 软件 I2C 总线编号。 */
#define I2C_BUS_1                           (0U)
#define I2C_BUS_2                           (1U)
#define I2C_BUS_3                           (2U)
#define I2C_BUS_COUNT                       (3U)

/* 无效 GPIO ID。 */
#define I2C_GPIO_ID_NULL                    (0xFFU)

/* 各路软件 I2C 对应的 GPIO 映射。 */
#define I2C1_DEP_SDA_GPIO_ID                GPIO_ID_I2C_SDA
#define I2C1_DEP_SCL_GPIO_ID                GPIO_ID_I2C_SCL
#define I2C2_DEP_SDA_GPIO_ID                I2C_GPIO_ID_NULL
#define I2C2_DEP_SCL_GPIO_ID                I2C_GPIO_ID_NULL
#define I2C3_DEP_SDA_GPIO_ID                I2C_GPIO_ID_NULL
#define I2C3_DEP_SCL_GPIO_ID                I2C_GPIO_ID_NULL

/* 软件 I2C 依赖的 GPIO 电平和延时配置。 */
#define I2C_DEP_GPIO_LEVEL_LOW              GPIO_LEVEL_LOW
#define I2C_DEP_GPIO_LEVEL_HIGH             GPIO_LEVEL_HIGH
#define I2C_CFG_SW_DELAY_LOOP               (3U)

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化所有 SDA/SCL 映射有效的软件 I2C 总线，并释放为高电平。 */
void STDLIB_I2C_Init(void);

/* 发送单字节数据帧：START + 地址 + 控制字节 + 数据 + STOP。 */
void STDLIB_I2C_WriteByte(uint8_t busId, uint8_t devAddr, uint8_t ctrlByte, uint8_t dataByte);

/* 发送连续数据帧：START + 地址 + 控制字节 + N 字节数据 + STOP。 */
void STDLIB_I2C_WriteData(uint8_t busId, uint8_t devAddr, uint8_t ctrlByte, const uint8_t *data, uint16_t len);

#endif
