#ifndef __DRIVER_A7670C_H__
#define __DRIVER_A7670C_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖（driver层向stdlib层索要）
 * 依赖：stdlib_usart
 *============================================================================*/

/* A7670C 使用的串口端口（UART_PORT1，即 USART1）。 */
#define A7670C_DEP_UART_PORT              UART_PORT1
/* 注册自定义字节回调。 */
#define A7670C_DEP_UART_SET_CUSTOM_CB(cb) STDLIB_USART_SetCustomCb(A7670C_DEP_UART_PORT, (cb))
/* 发送原始字节流。 */
#define A7670C_DEP_UART_SEND_RAW(d, l)    STDLIB_USART_SendRaw(A7670C_DEP_UART_PORT, (d), (l))

/*============================================================================
 * 向上提供（driver层向task/func层提供）
 *============================================================================*/

/* 接收行缓冲区最大长度（字节），含末尾 '\0'。 */
#define A7670C_RX_LINE_MAX_LEN  (128U)

/* 发送短信结果码。 */
#define A7670C_SMS_OK           (1U)   /* 发送成功 */
#define A7670C_SMS_FAIL         (0U)   /* 发送失败 */

/* A7670C 驱动状态枚举。 */
typedef enum {
  A7670C_STATE_IDLE = 0U, /* 空闲，可接受新任务 */
  A7670C_STATE_BUSY = 1U, /* 正在执行 AT 指令序列 */
} a7670cState_e;

/* A7670C 模块公有信息结构体。 */
typedef struct {
  a7670cState_e state;                        /* 当前驱动状态 */
  uint8_t       lastSmsResult;                /* 最近一次发送短信结果（A7670C_SMS_OK/FAIL） */
  uint8_t       rxLine[A7670C_RX_LINE_MAX_LEN]; /* 最近收到的完整响应行 */
  uint8_t       rxLineReady;                  /* 响应行就绪标志（1=有新行，0=无） */
} a7670cInfo_t;

extern a7670cInfo_t a7670cInfo;

/* 初始化 A7670C 驱动，注册串口字节回调。 */
void DRIVER_A7670C_Init(void);

/* 发送短信（PDU 模式，UCS2 编码，支持中文）。
 * phone   ：目标手机号字符串（如 "13800138000" 或 "+8613800138000"）。
 * content ：短信内容字符串（UTF-8 编码，支持中文，最多 70 个字符）。
 * 返回值  ：A7670C_SMS_OK（1）成功，A7670C_SMS_FAIL（0）失败。
 * 注意    ：本函数为阻塞调用，等待模块完整响应，超时则返回失败。
 */
uint8_t DRIVER_A7670C_SendSms(const char *phone, const char *content);

#endif
