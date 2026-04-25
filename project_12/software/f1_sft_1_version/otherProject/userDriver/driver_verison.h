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
 * 向下依赖（依赖 stdlib_usart）
 *============================================================================*/
/* MAIXCAM默认绑定USART2 */
#define VERISON_DEP_UART_PORT                       UART_PORT2
/* 注册USART2字节回调 */
#define VERISON_DEP_UART_SET_CUSTOM_CB(cb)          STDLIB_USART_SetCustomCb(VERISON_DEP_UART_PORT, (cb))

/*============================================================================
 * 向上提供
 *============================================================================*/
/* MAIXCAM协议数据段长度 */
#define VERISON_DATA_LEN                            (10U)

/* MAIXCAM解析结果结构体 */
typedef struct maixCamInfo{
  uint8_t ctrl;
  uint8_t data[VERISON_DATA_LEN];
  uint8_t crc8;
  uint8_t cnt;
  uint32_t crcErrTotalCnt;
  uint32_t rxTotalCnt;
}maixCamInfo_t;

typedef union {
    uint8_t  raw[10];        // 原始字节数组
    struct {
        int32_t var1;        // [0]~[3]
        int32_t var2;        // [4]~[7]
        uint8_t reserved[2]; // [8]~[9] 预留
    } field;
} realData_t;

extern maixCamInfo_t maixCamInfo;
extern realData_t versionRealData;

/* 初始化MAIXCAM驱动并注册USART2解析回调 */
void DRIVER_VERISON_Init(void);

/* 清空MAIXCAM缓存和解析状态 */
void DRIVER_VERISON_Reset(void);

/* 查询是否收到了新的MAIXCAM数据 */
uint8_t* DRIVER_VERISON_HasNewData(void);

/* 获取最近一次解析完成的MAIXCAM数据 */
uint8_t DRIVER_VERISON_GetMaixCamInfo(void);

#endif
