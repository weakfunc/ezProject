#include "stdlib_i2c.h"

/* 单条 I2C 总线的 SDA 和 SCL 映射。 */
typedef struct {
    uint8_t sdaGpioId;
    uint8_t sclGpioId;
} i2cBusMap_t;

/* 软件 I2C 总线到 GPIO 的映射表。 */
static const i2cBusMap_t i2cBusMap[I2C_BUS_COUNT] = {
    [I2C_BUS_1] = { I2C1_DEP_SDA_GPIO_ID, I2C1_DEP_SCL_GPIO_ID },
    [I2C_BUS_2] = { I2C2_DEP_SDA_GPIO_ID, I2C2_DEP_SCL_GPIO_ID },
    [I2C_BUS_3] = { I2C3_DEP_SDA_GPIO_ID, I2C3_DEP_SCL_GPIO_ID },
};

/* 检查总线 ID 是否在有效范围内。 */
static uint8_t __STDLIB_I2C_IsValidBusId(uint8_t busId){
    if(busId >= I2C_BUS_COUNT){
        return 0U;
    }
    return 1U;
}

/* 检查总线所需的 SDA 和 SCL 是否已完成映射。 */
static uint8_t __STDLIB_I2C_IsBusReady(uint8_t busId){
    if(__STDLIB_I2C_IsValidBusId(busId) == 0U){
        return 0U;
    }

    if((i2cBusMap[busId].sdaGpioId == I2C_GPIO_ID_NULL) ||
       (i2cBusMap[busId].sclGpioId == I2C_GPIO_ID_NULL)){
        return 0U;
    }

    return 1U;
}

/* 提供软件 I2C 位操作所需的短延时。 */
static void __STDLIB_I2C_Delay(void){
    volatile uint8_t t = I2C_CFG_SW_DELAY_LOOP;
    while(t--){
    }
}

/* 产生 I2C 起始条件。 */
static void __STDLIB_I2C_Start(const i2cBusMap_t *bus){
    STDLIB_COMMON_GpioWrite(bus->sdaGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
    STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
    __STDLIB_I2C_Delay();

    STDLIB_COMMON_GpioWrite(bus->sdaGpioId, I2C_DEP_GPIO_LEVEL_LOW);
    __STDLIB_I2C_Delay();

    STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_LOW);
    __STDLIB_I2C_Delay();
}

/* 产生 I2C 停止条件。 */
static void __STDLIB_I2C_Stop(const i2cBusMap_t *bus){
    STDLIB_COMMON_GpioWrite(bus->sdaGpioId, I2C_DEP_GPIO_LEVEL_LOW);
    STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
    __STDLIB_I2C_Delay();

    STDLIB_COMMON_GpioWrite(bus->sdaGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
    __STDLIB_I2C_Delay();
}

/* 兼容旧驱动行为，只保留 ACK 时钟节拍，不实际采样 ACK 电平。 */
static void __STDLIB_I2C_WaitAck(const i2cBusMap_t *bus){
    STDLIB_COMMON_GpioWrite(bus->sdaGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
    __STDLIB_I2C_Delay();
    STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
    __STDLIB_I2C_Delay();
    STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_LOW);
    __STDLIB_I2C_Delay();
}

/* 以 MSB first 方式发送 1 个字节。 */
static void __STDLIB_I2C_SendByte(const i2cBusMap_t *bus, uint8_t data){
    for(uint8_t i = 0U; i < 8U; i++){
        if((data & 0x80U) != 0U){
            STDLIB_COMMON_GpioWrite(bus->sdaGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
        } else {
            STDLIB_COMMON_GpioWrite(bus->sdaGpioId, I2C_DEP_GPIO_LEVEL_LOW);
        }

        __STDLIB_I2C_Delay();
        STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
        __STDLIB_I2C_Delay();
        STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_LOW);
        data <<= 1U;
    }
}

/* 将指定软件 I2C 总线释放为空闲状态。 */
static void __STDLIB_I2C_InitBus(const i2cBusMap_t *bus){
    STDLIB_COMMON_GpioWrite(bus->sdaGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
    STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
}

/* 初始化所有 SDA/SCL 映射有效的软件 I2C 总线。 */
void STDLIB_I2C_Init(void){
    for(uint8_t busId = 0U; busId < I2C_BUS_COUNT; busId++){
        if(__STDLIB_I2C_IsBusReady(busId) == 0U){
            continue;
        }

        __STDLIB_I2C_InitBus(&i2cBusMap[busId]);
    }
}

/* 发送 1 个控制字节和 1 个数据字节。 */
void STDLIB_I2C_WriteByte(uint8_t busId, uint8_t devAddr, uint8_t ctrlByte, uint8_t dataByte){
    const i2cBusMap_t *bus;
    if(__STDLIB_I2C_IsBusReady(busId) == 0U){
        return;
    }

    bus = &i2cBusMap[busId];
    __STDLIB_I2C_Start(bus);
    __STDLIB_I2C_SendByte(bus, devAddr);
    __STDLIB_I2C_WaitAck(bus);

    __STDLIB_I2C_SendByte(bus, ctrlByte);
    __STDLIB_I2C_WaitAck(bus);

    __STDLIB_I2C_SendByte(bus, dataByte);
    __STDLIB_I2C_WaitAck(bus);
    __STDLIB_I2C_Stop(bus);
}

/* 产生 ACK 信号。 */
static void __STDLIB_I2C_SendAck(const i2cBusMap_t *bus){
    STDLIB_COMMON_GpioWrite(bus->sdaGpioId, I2C_DEP_GPIO_LEVEL_LOW);
    __STDLIB_I2C_Delay();
    STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
    __STDLIB_I2C_Delay();
    STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_LOW);
    __STDLIB_I2C_Delay();
}

/* 产生 NACK 信号。 */
static void __STDLIB_I2C_SendNack(const i2cBusMap_t *bus){
    STDLIB_COMMON_GpioWrite(bus->sdaGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
    __STDLIB_I2C_Delay();
    STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
    __STDLIB_I2C_Delay();
    STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_LOW);
    __STDLIB_I2C_Delay();
}

/* 释放 SDA 后以 MSB first 方式接收 1 个字节。 */
static uint8_t __STDLIB_I2C_RecvByte(const i2cBusMap_t *bus){
    uint8_t data = 0U;
    uint8_t i;

    STDLIB_COMMON_GpioWrite(bus->sdaGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
    for(i = 0U; i < 8U; i++){
        data <<= 1U;
        __STDLIB_I2C_Delay();
        STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_HIGH);
        __STDLIB_I2C_Delay();
        if(STDLIB_COMMON_GpioRead(bus->sdaGpioId) != I2C_DEP_GPIO_LEVEL_LOW){
            data |= 0x01U;
        }
        STDLIB_COMMON_GpioWrite(bus->sclGpioId, I2C_DEP_GPIO_LEVEL_LOW);
    }
    return data;
}

/* 连续发送多个数据字节。 */
void STDLIB_I2C_WriteData(uint8_t busId, uint8_t devAddr, uint8_t ctrlByte, const uint8_t *data, uint16_t len){
    const i2cBusMap_t *bus;
    if(__STDLIB_I2C_IsBusReady(busId) == 0U){
        return;
    }

    bus = &i2cBusMap[busId];
    __STDLIB_I2C_Start(bus);
    __STDLIB_I2C_SendByte(bus, devAddr);
    __STDLIB_I2C_WaitAck(bus);

    __STDLIB_I2C_SendByte(bus, ctrlByte);
    __STDLIB_I2C_WaitAck(bus);

    for(uint16_t i = 0U; i < len; i++){
        __STDLIB_I2C_SendByte(bus, data[i]);
        __STDLIB_I2C_WaitAck(bus);
    }
    __STDLIB_I2C_Stop(bus);
}

/* 先写寄存器地址，再重复起始读取 N 字节。 */
void STDLIB_I2C_ReadData(uint8_t busId, uint8_t devAddrWrite, uint8_t regAddr, uint8_t *data, uint16_t len){
    const i2cBusMap_t *bus;
    uint16_t i;

    if(__STDLIB_I2C_IsBusReady(busId) == 0U) return;
    if((data == NULL) || (len == 0U)) return;

    bus = &i2cBusMap[busId];

    /* 写阶段：发送寄存器地址 */
    __STDLIB_I2C_Start(bus);
    __STDLIB_I2C_SendByte(bus, devAddrWrite);
    __STDLIB_I2C_WaitAck(bus);
    __STDLIB_I2C_SendByte(bus, regAddr);
    __STDLIB_I2C_WaitAck(bus);

    /* 读阶段：重复起始后切换为读地址 */
    __STDLIB_I2C_Start(bus);
    __STDLIB_I2C_SendByte(bus, devAddrWrite | 0x01U);
    __STDLIB_I2C_WaitAck(bus);

    for(i = 0U; i < len; i++){
        data[i] = __STDLIB_I2C_RecvByte(bus);
        if(i < (len - 1U)){
            __STDLIB_I2C_SendAck(bus);
        } else {
            __STDLIB_I2C_SendNack(bus);
        }
    }

    __STDLIB_I2C_Stop(bus);
}
