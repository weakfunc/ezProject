/*============================================================================
 * MAIXCAM串口协议（固定16字节）
 * [0]     0x55        包头1
 * [1]     0xAA        包头2
 * [2]     0x01        控制字段
 * [3~12]  data        二维码内容（10字节，不足补0x00）
 * [13]    CRC8        0x00时直接通过，否则校验[0]~[12]（多项式0x07）
 * [14]    CNT         包计数（0x00~0xFF循环递增）
 * [15]    0xFF        包尾
 *============================================================================*/

#ifndef __DRIVER_VERISON_H__
#define __DRIVER_VERISON_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 *============================================================================*/
/* MAIXCAM默认绑定USART2 */
#define VERISON_DEP_UART_PORT                       UART_PORT2
/* 注册USART2字节回调 */
#define VERISON_DEP_UART_SET_CUSTOM_CB(cb)          STDLIB_USART_SetCustomCb(VERISON_DEP_UART_PORT, (cb))

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/
/* MAIXCAM协议数据段长度 */
#define VERISON_DATA_LEN                            (10U)

/* MAIXCAM模块信息结构体 */
typedef struct {
  uint8_t           ctrl;             /* 控制字段 */
  uint8_t           data[VERISON_DATA_LEN]; /* 原始数据段 */
  uint8_t           crc8;             /* CRC8校验值 */
  uint8_t           cnt;              /* 包计数 */
  uint32_t          crcErrTotalCnt;   /* CRC错误包总计数 */
  uint32_t          rxTotalCnt;       /* 接收完整包总计数 */
  uint8_t           hasNewData;       /* 是否有新数据标志 */
  standardFrame_t   realData;         /* 解析后数据字段 */
} verisonInfo_t;

extern verisonInfo_t verisonInfo;

/* 初始化MAIXCAM驱动并注册USART2解析回调 */
void DRIVER_VERISON_Init(void);

/* 清空MAIXCAM缓存和解析状态 */
void DRIVER_VERISON_Reset(void);

/* 查询是否收到了新的MAIXCAM数据 */
uint8_t* DRIVER_VERISON_HasNewData(void);

/* 获取最近一次解析完成的MAIXCAM数据 */
uint8_t DRIVER_VERISON_GetMaixCamInfo(void);

#endif
