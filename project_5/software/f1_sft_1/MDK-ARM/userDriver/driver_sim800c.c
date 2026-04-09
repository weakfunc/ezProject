/*============================================================================
 * README
 * driver_sim800c：SIM800C GSM 模块驱动
 * 通过 USART1 发送 AT 指令，实现短信发送功能。
 * 短信编码：TEXT 模式，GSM-7 字符集（不支持中文短信）。
 *============================================================================*/

#include "driver_sim800c.h"
#include "cmsis_os.h"  /* 仅用于 osDelay，使阻塞等待期间让出 CPU 给调度器 */
#include <string.h>

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* AT 指令发送后等待响应的超时时间（毫秒）。 */
#define SIM800C_TIMEOUT_MS          (15000U)

/* 短信内容最大长度（字节，不含 '\0'）。 */
#define SIM800C_SMS_CONTENT_MAX_LEN (160U)

/* 手机号最大长度（字节，不含 '\0'）。 */
#define SIM800C_PHONE_MAX_LEN       (20U)

/* Ctrl+Z（0x1A），用于结束短信内容输入。 */
#define SIM800C_CTRL_Z              (0x1AU)

/*============================================================================
 * 私有类型定义
 *============================================================================*/

/* AT 指令等待的响应类型。 */
typedef enum {
  WAIT_OK,        /* 等待 "OK" */
  WAIT_PROMPT,    /* 等待 ">" 提示符 */
  WAIT_SEND_OK,   /* 等待 "+CMGS:" 发送成功回应 */
} waitType_e;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* SIM800C 公有信息结构体（extern 在 .h 中声明）。 */
sim800cInfo_t sim800cInfo;

/* 接收行拼接缓冲区（私有）。 */
static uint8_t rxBuf[SIM800C_RX_LINE_MAX_LEN];

/* 当前接收缓冲区写入索引。 */
static uint8_t rxIdx = 0U;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* USART1 自定义字节回调：逐字节拼接响应行。
 * 以 '\n' 作为行结束，将完整行复制到 sim800cInfo.rxLine 并置位就绪标志。
 * '>' 提示符单独作为一行处理（无换行符）。
 */
static void __DRIVER_SIM800C_UartByteCallback(uint8_t port, uint8_t byte){
  if(port != SIM800C_DEP_UART_PORT) return;

  /* '>' 提示符无换行，单独触发行就绪。 */
  if(byte == '>'){
    rxBuf[0] = '>';
    rxBuf[1] = '\0';
    memcpy(sim800cInfo.rxLine, rxBuf, 2U);
    sim800cInfo.rxLineReady = 1U;
    rxIdx = 0U;
    return;
  }

  /* 忽略 '\r'，保持缓冲区内容为纯文本。 */
  if(byte == '\r') return;

  if(byte == '\n'){
    /* 行结束：将缓冲内容发布为就绪行（空行直接丢弃）。 */
    if(rxIdx > 0U){
      rxBuf[rxIdx] = '\0';
      memcpy(sim800cInfo.rxLine, rxBuf,
             (rxIdx + 1U < SIM800C_RX_LINE_MAX_LEN) ? (rxIdx + 1U) : SIM800C_RX_LINE_MAX_LEN);
      sim800cInfo.rxLine[SIM800C_RX_LINE_MAX_LEN - 1U] = '\0';
      sim800cInfo.rxLineReady = 1U;
    }
    rxIdx = 0U;
    return;
  }

  /* 普通字符：追加到缓冲区，防止溢出。 */
  if(rxIdx < (SIM800C_RX_LINE_MAX_LEN - 1U)){
    rxBuf[rxIdx++] = byte;
  }
}

/* 发送原始字符串（不加换行）。 */
static void __DRIVER_SIM800C_SendStr(const char *str){
  STDLIB_USART_SendRaw(SIM800C_DEP_UART_PORT,
                       (uint8_t *)str,
                       (uint16_t)strlen(str));
}

/* 发送 AT 指令行（自动追加 "\r\n"）。 */
static void __DRIVER_SIM800C_SendCmd(const char *cmd){
  static const uint8_t crlf[2] = {'\r', '\n'};
  __DRIVER_SIM800C_SendStr(cmd);
  STDLIB_USART_SendRaw(SIM800C_DEP_UART_PORT, (uint8_t *)crlf, 2U);
}

/* 等待指定类型响应，超时返回 0，匹配成功返回 1。
 * 轮询方式：依赖 rxLineReady 标志，不使用 RTOS 阻塞。
 */
static uint8_t __DRIVER_SIM800C_WaitResp(waitType_e waitType, uint32_t timeoutMs){
  uint32_t startMs = HAL_GetTick();

  while((HAL_GetTick() - startMs) < timeoutMs){
    osDelay(1U); /* 让出 CPU，避免忙等期间饿死其他任务 */
    if(sim800cInfo.rxLineReady){
      sim800cInfo.rxLineReady = 0U;
      switch(waitType){
        case WAIT_OK:
          if(strncmp((char *)sim800cInfo.rxLine, "OK", 2U) == 0){
            return 1U;
          }
          /* 收到 "ERROR" 则提前退出。 */
          if(strncmp((char *)sim800cInfo.rxLine, "ERROR", 5U) == 0){
            return 0U;
          }
          break;

        case WAIT_PROMPT:
          if(sim800cInfo.rxLine[0] == '>'){
            return 1U;
          }
          break;

        case WAIT_SEND_OK:
          /* "+CMGS:" 开头表示短信已提交至网络。 */
          if(strncmp((char *)sim800cInfo.rxLine, "+CMGS:", 6U) == 0){
            return 1U;
          }
          if(strncmp((char *)sim800cInfo.rxLine, "ERROR", 5U) == 0){
            return 0U;
          }
          break;

        default:
          break;
      }
    }
  }
  return 0U;
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化 SIM800C 驱动，注册 USART1 字节回调。 */
void DRIVER_SIM800C_Init(void){
  memset(&sim800cInfo, 0, sizeof(sim800cInfo));
  rxIdx = 0U;
  memset(rxBuf, 0, sizeof(rxBuf));
  SIM800C_DEP_UART_SET_CUSTOM_CB(__DRIVER_SIM800C_UartByteCallback);
}

/* 发送短信。
 * phone   ：目标手机号字符串（如 "13800138000"）。
 * content ：短信内容字符串，GSM-7 字符集，最长 SIM800C_SMS_CONTENT_MAX_LEN 字节。
 * 返回值  ：SIM800C_SMS_OK（1）= 成功，SIM800C_SMS_FAIL（0）= 失败。
 * 注意    ：本函数为阻塞调用，期间占用 CPU 等待模块响应，请勿在中断中调用。
 */
uint8_t DRIVER_SIM800C_SendSms(const char *phone, const char *content){
  static char cmdBuf[SIM800C_PHONE_MAX_LEN + 16U]; /* "AT+CMGS=\"<phone>\"\r\n" */
  static uint8_t ctrlZ[1] = {SIM800C_CTRL_Z};

  if((phone == NULL) || (content == NULL)){
    return SIM800C_SMS_FAIL;
  }

  sim800cInfo.state = SIM800C_STATE_BUSY;

  /* 步骤1：设置 TEXT 模式（AT+CMGF=1）。 */
  __DRIVER_SIM800C_SendCmd("AT+CMGF=1");
  if(__DRIVER_SIM800C_WaitResp(WAIT_OK, SIM800C_TIMEOUT_MS) == 0U){
    sim800cInfo.state = SIM800C_STATE_IDLE;
    sim800cInfo.lastSmsResult = SIM800C_SMS_FAIL;
    return SIM800C_SMS_FAIL;
  }

  /* 步骤2：发送 AT+CMGS 指令，等待 ">" 提示符。 */
  memset(cmdBuf, 0, sizeof(cmdBuf));
  /* 拼接：AT+CMGS="<phone>" */
  strncpy(cmdBuf, "AT+CMGS=\"", 10U);
  strncat(cmdBuf, phone, SIM800C_PHONE_MAX_LEN);
  strncat(cmdBuf, "\"", 2U);
  __DRIVER_SIM800C_SendCmd(cmdBuf);

  if(__DRIVER_SIM800C_WaitResp(WAIT_PROMPT, SIM800C_TIMEOUT_MS) == 0U){
    /* 等待提示符超时，发送 Ctrl+Z 中止，恢复模块状态。 */
    STDLIB_USART_SendRaw(SIM800C_DEP_UART_PORT, ctrlZ, 1U);
    sim800cInfo.state = SIM800C_STATE_IDLE;
    sim800cInfo.lastSmsResult = SIM800C_SMS_FAIL;
    return SIM800C_SMS_FAIL;
  }

  /* 步骤3：发送短信内容，内容截断至最大长度，末尾发送 Ctrl+Z 提交。 */
  __DRIVER_SIM800C_SendStr(content);
  STDLIB_USART_SendRaw(SIM800C_DEP_UART_PORT, ctrlZ, 1U);

  /* 步骤4：等待 "+CMGS:" 响应，确认短信已提交至网络。 */
  if(__DRIVER_SIM800C_WaitResp(WAIT_SEND_OK, SIM800C_TIMEOUT_MS) == 0U){
    sim800cInfo.state = SIM800C_STATE_IDLE;
    sim800cInfo.lastSmsResult = SIM800C_SMS_FAIL;
    return SIM800C_SMS_FAIL;
  }

  sim800cInfo.state = SIM800C_STATE_IDLE;
  sim800cInfo.lastSmsResult = SIM800C_SMS_OK;
  return SIM800C_SMS_OK;
}
