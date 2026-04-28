#ifndef __DRIVER_XRVOICE_H__
#define __DRIVER_XRVOICE_H__

#include <stdint.h>
#include "stdlib_i2c.h"

/*============================================================================
 * 向下依赖（driver层向stdlib层索要）
 * 依赖：stdlib_i2c
 *============================================================================*/

/* XRVoice 语音模块使用的 I2C 总线编号，默认 I2C_BUS_1 */
#define XRVOICE_DEP_I2C_BUS_ID         I2C_BUS_1

/* XRVoice 模块 7 位 I2C 地址为 0x20，stdlib_i2c 需要 8 位写地址（含 R/W 位）：0x20 << 1 = 0x40 */
#define XRVOICE_CFG_ADDR_WRITE         (0x40U)

/* I2C mem 模式读取：从寄存器 reg 读取 len 字节到 buf */
#define XRVOICE_DEP_I2C_READ(reg, buf, len) \
  STDLIB_I2C_ReadData(XRVOICE_DEP_I2C_BUS_ID, XRVOICE_CFG_ADDR_WRITE, (uint8_t)(reg), (buf), (uint16_t)(len))

/* I2C mem 模式写入：向寄存器 reg 写入 len 字节数据 data */
#define XRVOICE_DEP_I2C_WRITE(reg, data, len) \
  STDLIB_I2C_WriteData(XRVOICE_DEP_I2C_BUS_ID, XRVOICE_CFG_ADDR_WRITE, (uint8_t)(reg), (data), (uint16_t)(len))

/*============================================================================
 * 向上提供（driver层向func/task层提供）
 *============================================================================*/

/* 协议帧长度（字节），上层声明帧缓冲区时使用 */
#define XRVOICE_PROTO_FRAME_LEN        (5U)

/* XRVoice 模块公有信息结构体
 * 模块识别到语音命令后，通过 DRIVER_XRVOICE_Poll() 更新本结构体。
 * 上层读取 cmdReady 后应将其清零以等待下一条命令。
 */
typedef struct {
  uint8_t cmdReady;    /* 新命令就绪标志：1=有新语音命令待处理，0=无 */
  uint8_t lastCmdType; /* 最近收到的命令类别（AA 55 格式的 Byte3） */
  uint8_t lastCmdId;   /* 最近收到的命令协议编号（AA 55 格式的 Byte4） */
} xrVoiceInfo_t;

extern xrVoiceInfo_t xrVoiceInfo;

/* 初始化 XRVoice 驱动，清零公有信息结构体并初始化 I2C 总线 */
void DRIVER_XRVOICE_Init(void);

/* 轮询一次 I2C，若读到有效语音命令则更新 xrVoiceInfo 并返回 1，否则返回 0 */
uint8_t DRIVER_XRVOICE_Poll(void);

/* 向模块发送 5 字节协议帧，通知模块播放对应语音。
 * frame ：指向 XRVOICE_PROTO_FRAME_LEN 字节协议帧的指针
 */
void DRIVER_XRVOICE_SendCmd(const uint8_t *frame);

/* 根据语义编号（1~53）查找对应的 5 字节协议帧，拷贝到 frameOut。
 * semanticId ：语义编号（1~53，与模块配置一致）
 * frameOut   ：输出缓冲区，至少 XRVOICE_PROTO_FRAME_LEN 字节
 * 返回值     ：1=找到，0=语义编号超出范围
 */
uint8_t DRIVER_XRVOICE_FindBySemanticId(uint8_t semanticId, uint8_t *frameOut);

/* 根据接收到的 cmdType 和 cmdId（AA 55 格式）查找对应的语义编号。
 * cmdType    ：AA 55 帧的 Byte3（命令类别）
 * cmdId      ：AA 55 帧的 Byte4（命令编号）
 * semanticId ：输出参数，找到时写入语义编号（1~10）
 * 返回值     ：1=找到，0=未找到
 */
uint8_t DRIVER_XRVOICE_FindByCmdTypeId(uint8_t cmdType, uint8_t cmdId, uint8_t *semanticId);

#endif
