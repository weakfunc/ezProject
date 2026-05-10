/*============================================================================
 * MAIXCAM串口协议（固定16字节）
 * [0]     0x55        包头1
 * [1]     0xAA        包头2
 * [2]     0x01        控制字段
 * [3]     obj1        分类1编码（无目标/冷却中=0xFF）
 * [4]     obj2        分类2编码（无目标/冷却中=0xFF）
 * [5]     fps         摄像头帧率（uint8）
 * [6~12]  0x00        保留
 * [13]    CRC8        0x00时直接通过，否则校验[0]~[12]（多项式0x07）
 * [14]    CNT         包计数（0x00~0xFF循环递增）
 * [15]    0xFF        包尾
 *============================================================================*/

#ifndef __DRIVER_VERISON_H__
#define __DRIVER_VERISON_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 * 依赖：stdlib_usart
 *============================================================================*/
/* MAIXCAM默认绑定USART2 */
#define VERISON_DEP_UART_PORT                       UART_PORT2
/* 注册USART2字节回调 */
#define VERISON_DEP_UART_SET_CUSTOM_CB(cb)          STDLIB_USART_SetCustomCb(VERISON_DEP_UART_PORT, (cb))

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/
/* obj1/obj2无效值（无目标/冷却中） */
#define VERISON_INVALID_OBJ                         (0xFFU)

/* obj1 type counter indexes */
#define VERISON_OBJ_TYPE_A                          (0U)
#define VERISON_OBJ_TYPE_B                          (1U)
#define VERISON_OBJ_TYPE_C                          (2U)
#define VERISON_OBJ_TYPE_COUNT                      (3U)

/* MAIXCAM模块信息结构体 */
typedef struct {
  uint8_t  obj1;         /* 当前帧分类1编码（0xFF=无目标/冷却中） */
  uint8_t  obj2;         /* 当前帧分类2编码（0xFF=无目标/冷却中） */
  uint8_t  fps;          /* 摄像头帧率 */
  uint8_t  hasValidData; /* 当前帧有效数据标志（obj1 != 0xFF时为1） */
  uint8_t  hasNewData;   /* 新数据标志（Updata更新后置1，下次Updata调用时刷新） */
  uint32_t rxTotalCnt;   /* 摄像头数据帧总接收计数（CRC通过即计，不区分obj1是否有效） */
  uint32_t localPktCnt;  /* STM32本地有效包计数（仅obj1 != 0xFF时累加） */
  uint8_t  camCnt;       /* 当前帧摄像头发送CNT值 */
  uint32_t objTypeCnt[VERISON_OBJ_TYPE_COUNT]; /* A/B/C valid obj1 counters */
} verisonInfo_t;

extern verisonInfo_t verisonInfo;

/* 初始化MAIXCAM驱动并注册USART2解析回调 */
void DRIVER_VERISON_Init(void);

/* 更新verisonInfo，在userTask中10ms周期调用 */
void DRIVER_VERISON_Updata(void);

#endif
