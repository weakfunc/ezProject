#ifndef I2C_INTERFACE_H
#define I2C_INTERFACE_H

#include "voice_protocol.h"
#include <stddef.h>
#include <stdint.h>

// I2C通信处理函数
void processI2CCommunication(void);
void log_print_buf(uint8_t *buf, size_t len);

// I2C通信接口
int i2c_comm_init(void);
int i2c_comm_read(uint8_t* buffer, size_t len);
int i2c_comm_write(uint8_t* data, size_t len);

// I2C mem方式的读写接口
int i2c_mem_read(uint16_t mem_addr, uint8_t* buffer, size_t len);
int i2c_mem_write(uint16_t mem_addr, uint8_t* data, size_t len);

#endif
