#ifndef __DRIVER_TJCLCD_H__
#define __DRIVER_TJCLCD_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 * 依赖：stdlib_usart
 *============================================================================*/
#define TJCLCD_DEP_UART_PORT                    UART_PORT3
#define TJCLCD_DEP_UART_SET_CUSTOM_CB(cb)       STDLIB_USART_SetCustomCb(TJCLCD_DEP_UART_PORT, (cb))
#define TJCLCD_DEP_UART_SEND_RAW(data, len)     STDLIB_USART_SendRaw(TJCLCD_DEP_UART_PORT, (data), (len))

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/
#define TJCLCD_FRAME_LEN             (16U)  /* 固定帧总长度 */
#define TJCLCD_DATA_LEN              (10U)  /* 数据内容长度（[3]~[12]） */
#define TJCLCD_CTRL_NAME_MAX_LEN     (16U)  /* 文本控件名最大长度 */
#define TJCLCD_TEXT_CONTENT_MAX_LEN  (64U)  /* 文本控件内容最大字节数（GB2312编码） */

/* TJC串口屏数据帧结构体 */
typedef struct {
  uint8_t ctrl;                   /* 控制字段 [2]，范围 0x05~0x08 */
  uint8_t data[TJCLCD_DATA_LEN];  /* ASCII数据内容，不足补0x00 */
} tjcLcdFrame_t;

/* TJC串口屏模块信息结构体 */
typedef struct {
  tjcLcdFrame_t rxFrame;    /* 最近一次接收帧 */
  uint8_t       hasNewData; /* 是否有新数据标志 */
} tjcLcdInfo_t;

extern tjcLcdInfo_t tjcLcdInfo;

/* 初始化TJC串口屏驱动，绑定USART3字节回调 */
void DRIVER_TJCLCD_Init(void);

/* 清空接收缓存与解析状态 */
void DRIVER_TJCLCD_Reset(void);

/* 发送一帧数据至TJC串口屏 */
void DRIVER_TJCLCD_Send(const tjcLcdFrame_t *frame);

/* 查询是否有新数据帧到达 */
uint8_t DRIVER_TJCLCD_HasNewData(void);

/* 获取最近一次接收到的数据帧，并清除更新标志 */
uint8_t DRIVER_TJCLCD_GetRxFrame(tjcLcdFrame_t *frame);

/* 更新TJC串口屏文本控件内容，格式：控件名.txt="内容"+0xFF 0xFF 0xFF，内容为GB2312编码字节流 */
void DRIVER_TJCLCD_SendText(const char *ctrlName, const uint8_t *content, uint8_t contentLen);

#endif
