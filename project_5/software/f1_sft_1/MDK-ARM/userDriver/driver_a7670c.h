#ifndef __DRIVER_A7670C_H__
#define __DRIVER_A7670C_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖（driver 层向 stdlib 层索要）
 *============================================================================*/

#define A7670C_DEP_UART_PORT              UART_PORT1
#define A7670C_DEP_UART_SET_CUSTOM_CB(cb) STDLIB_USART_SetCustomCb(A7670C_DEP_UART_PORT, (cb))
#define A7670C_DEP_UART_SEND_RAW(d, l)    STDLIB_USART_SendRaw(A7670C_DEP_UART_PORT, (d), (l))

/*============================================================================
 * 向上提供（driver 层向 task/func 层提供）
 *============================================================================*/

/* 接收行缓冲区最大长度（含 '\0'）*/
#define A7670C_RX_LINE_MAX_LEN    (128U)

/* 发送短信结果码 */
#define A7670C_SMS_OK             (1U)
#define A7670C_SMS_FAIL           (0U)

/*============================================================================
 * 自检诊断结构体
 *
 * 初始化时执行 4 步自检，结果保存在 a7670cInfo.diag。
 * 调试时在 Watch 窗口展开 a7670cInfo.diag 查看：
 *
 *   allPass=1                   → 四步全部通过，模块完全就绪
 *   step1Result=FAIL            → AT 握手失败，检查串口接线/波特率
 *   step2Result=FAIL            → SIM 卡未识别，检查卡座/卡类型
 *   step3Result=PASS,csqRssi=99 → 天线未接或断路
 *   step3Result=PASS,csqRssi<10 → 信号极弱，检查天线和环境
 *   step4Result=FAIL,ceregStat=6→ 物联网卡/纯流量卡，无短信权限
 *   step4Result=FAIL,ceregStat=2→ 仍在搜网，可增大 A7670C_CEREG_TIMEOUT_MS
 *   stepXRaw                    → 该步模块原始回复字符串
 *============================================================================*/

#define A7670C_DIAG_PASS     (1U)
#define A7670C_DIAG_FAIL     (0U)
#define A7670C_DIAG_PENDING  (0xFFU)

#define A7670C_IMSI_MAX_LEN      (16U)
#define A7670C_DIAG_RAW_MAX_LEN  (A7670C_RX_LINE_MAX_LEN)

typedef struct {
  /* STEP1: AT 握手 */
  uint8_t step1Result;
  char    step1Raw[A7670C_DIAG_RAW_MAX_LEN];

  /* STEP2: AT+CIMI 读取 IMSI */
  uint8_t step2Result;
  char    step2Raw[A7670C_DIAG_RAW_MAX_LEN];
  char    imsi[A7670C_IMSI_MAX_LEN];

  /* STEP3: AT+CSQ 信号强度 */
  uint8_t step3Result;
  char    step3Raw[A7670C_DIAG_RAW_MAX_LEN];
  uint8_t csqRssi;            /* 0~31 有效，99=无信号/天线异常 */

  /* STEP4: AT+CEREG? 4G 注册状态 */
  uint8_t step4Result;
  char    step4Raw[A7670C_DIAG_RAW_MAX_LEN];
  uint8_t ceregStat;          /* 1/5=注册成功，2=搜网中，6=物联网卡 */

  /* 汇总 */
  uint8_t allPass;            /* 1=四步全部通过 */
} a7670cDiag_t;

/*============================================================================
 * 驱动状态与公有信息
 *============================================================================*/

typedef enum {
  A7670C_STATE_IDLE = 0U,
  A7670C_STATE_BUSY = 1U,
} a7670cState_e;

typedef struct {
  a7670cState_e state;
  uint8_t       lastSmsResult;
  uint8_t       rxLine[A7670C_RX_LINE_MAX_LEN];
  uint8_t       rxLineReady;
  a7670cDiag_t  diag;
} a7670cInfo_t;

extern a7670cInfo_t a7670cInfo;

/*============================================================================
 * API
 *============================================================================*/

/*
 * DRIVER_A7670C_Init
 *
 * 初始化 A7670C 驱动：
 *   1. 注册串口字节回调。
 *   2. 等待模块启动完成（"SMS DONE"，最长 10s）。
 *   3. 执行 4 步自检状态机，结果写入 a7670cInfo.diag。
 *
 * 阻塞总时长：约 10s（启动）+ 最长 45s（自检）。
 * 请在 RTOS 任务初始化阶段调用，不可在中断中调用。
 */
void DRIVER_A7670C_Init(void);

/*
 * DRIVER_A7670C_SendSms
 *
 * 发送短信（PDU 模式，UCS2 编码，支持中文）。
 *   phone   ：目标手机号（如 "13800138000" 或 "+8613800138000"）
 *   content ：短信内容（UTF-8，最多 70 字符）
 *   返回值  ：A7670C_SMS_OK(1)=成功，A7670C_SMS_FAIL(0)=失败
 *
 * 内部先等待 4G 网络注册完成（最长 30s），再执行发送流程。
 * 阻塞调用，请勿在中断中调用。
 */
uint8_t DRIVER_A7670C_SendSms(const char *phone, const char *content);

#endif /* __DRIVER_A7670C_H__ */
