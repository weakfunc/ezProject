/*============================================================================
 * driver_a7670c.c — A7670C 4G 模块驱动
 *
 * 功能：
 *   1. Init：等待模块启动（SMS DONE），随后执行 4 步自检并记录诊断信息。
 *   2. SendSms：发送前等待 4G 网络注册完成，再以 PDU/UCS2 模式发送短信。
 *
 * 自检诊断（查看 a7670cInfo.diag 快速定位问题）：
 *   step1Result=FAIL            → 串口未通，检查接线/波特率
 *   step2Result=FAIL            → SIM 卡未识别，检查卡座/卡类型
 *   step3Result=PASS,csqRssi=99 → 天线未接
 *   step4Result=FAIL,ceregStat=6→ 物联网卡/流量卡，无短信权限
 *   step4Result=FAIL,ceregStat=2→ 仍在搜网，可增大 A7670C_CEREG_TIMEOUT_MS
 *   allPass=1                   → 模块完全就绪
 *
 * 接收行队列：
 *   模块对 AT+CIMI/AT+CSQ 等命令连续回复数据行+OK，间隔<1ms。
 *   单缓冲方案数据行被 OK 覆盖，改用 RX_QUEUE_SIZE=8 的环形队列。
 *
 * 依赖：stdlib_usart，cmsis_os，HAL
 *============================================================================*/

#include "driver_a7670c.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*============================================================================
 * 私有宏
 *============================================================================*/

#define A7670C_TIMEOUT_MS        (5000U)   /* 普通 AT 命令超时 */
#define A7670C_CEREG_TIMEOUT_MS  (30000U)  /* 网络注册等待超时 */
#define A7670C_BOOT_TIMEOUT_MS   (10000U)  /* 等待模块启动超时 */
#define A7670C_UCS2_MAX_CHARS    (70U)     /* 单条短信最大字符数 */
#define A7670C_PHONE_MAX_DIGITS  (20U)     /* 手机号最大位数 */
#define A7670C_PDU_HEX_MAX_LEN   (320U)    /* PDU 十六进制串最大长度 */
#define A7670C_CMD_BUF_LEN       (16U)     /* AT+CMGS=xxx 缓冲 */
#define A7670C_CTRL_Z            (0x1AU)   /* Ctrl+Z，触发短信发送 */
#define RX_QUEUE_SIZE            (4U)      /* 接收行队列深度 */

/*============================================================================
 * 私有类型
 *============================================================================*/

typedef enum {
  WAIT_OK,       /* 等待 "OK" */
  WAIT_PROMPT,   /* 等待 ">" */
  WAIT_SEND_OK,  /* 等待 "+CMGS:" */
  WAIT_LINE,     /* 等待任意数据行（跳过 OK） */
} waitType_e;

typedef enum {
  DIAG_STATE_STEP1_SEND = 0U,
  DIAG_STATE_STEP1_WAIT,
  DIAG_STATE_STEP2_SEND,
  DIAG_STATE_STEP2_WAIT,
  DIAG_STATE_STEP3_SEND,
  DIAG_STATE_STEP3_WAIT,
  DIAG_STATE_STEP4_SEND,
  DIAG_STATE_STEP4_WAIT,
  DIAG_STATE_DONE,
} diagState_e;

/* 接收行环形队列 */
typedef struct {
  uint8_t lines[RX_QUEUE_SIZE][A7670C_RX_LINE_MAX_LEN];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
} rxLineQueue_t;

/*============================================================================
 * 私有变量
 *============================================================================*/

a7670cInfo_t         a7670cInfo;
static uint8_t       rxBuf[A7670C_RX_LINE_MAX_LEN];
static uint8_t       rxIdx = 0U;
static rxLineQueue_t rxLineQueue;

/*============================================================================
 * UART 收发基础层
 *============================================================================*/

/* 字节回调：拼行后推入队列，同步更新 rxLine 供 Watch 窗口观察 */
static void __DRIVER_A7670C_UartByteCallback(uint8_t port, uint8_t byte)
{
  if (port != A7670C_DEP_UART_PORT) return;

  /* '>' 无换行，单独推入队列 */
  if (byte == '>') {
    if (rxLineQueue.count < RX_QUEUE_SIZE) {
      rxLineQueue.lines[rxLineQueue.tail][0] = '>';
      rxLineQueue.lines[rxLineQueue.tail][1] = '\0';
      rxLineQueue.tail  = (uint8_t)((rxLineQueue.tail + 1U) % RX_QUEUE_SIZE);
      rxLineQueue.count++;
    }
    a7670cInfo.rxLine[0]   = '>';
    a7670cInfo.rxLine[1]   = '\0';
    a7670cInfo.rxLineReady = 1U;
    rxIdx = 0U;
    return;
  }

  if (byte == '\r') return;

  if (byte == '\n') {
    if (rxIdx > 0U) {
      rxBuf[rxIdx] = '\0';
      uint16_t len = (rxIdx + 1U < A7670C_RX_LINE_MAX_LEN)
                     ? (rxIdx + 1U) : A7670C_RX_LINE_MAX_LEN;
      if (rxLineQueue.count < RX_QUEUE_SIZE) {
        memcpy(rxLineQueue.lines[rxLineQueue.tail], rxBuf, len);
        rxLineQueue.lines[rxLineQueue.tail][A7670C_RX_LINE_MAX_LEN - 1U] = '\0';
        rxLineQueue.tail  = (uint8_t)((rxLineQueue.tail + 1U) % RX_QUEUE_SIZE);
        rxLineQueue.count++;
      }
      memcpy(a7670cInfo.rxLine, rxBuf, len);
      a7670cInfo.rxLine[A7670C_RX_LINE_MAX_LEN - 1U] = '\0';
      a7670cInfo.rxLineReady = 1U;
    }
    rxIdx = 0U;
    return;
  }

  if (rxIdx < (A7670C_RX_LINE_MAX_LEN - 1U)) {
    rxBuf[rxIdx++] = byte;
  }
}

static void __DRIVER_A7670C_SendStr(const char *str)
{
  A7670C_DEP_UART_SEND_RAW((uint8_t *)str, (uint16_t)strlen(str));
}

static void __DRIVER_A7670C_SendCmd(const char *cmd)
{
  static const uint8_t crlf[2] = {'\r', '\n'};
  __DRIVER_A7670C_SendStr(cmd);
  A7670C_DEP_UART_SEND_RAW((uint8_t *)crlf, 2U);
}

/*============================================================================
 * 响应等待
 *============================================================================*/

static uint8_t __DRIVER_A7670C_Dequeue(uint8_t *outLine)
{
  if (rxLineQueue.count == 0U) return 0U;
  memcpy(outLine, rxLineQueue.lines[rxLineQueue.head], A7670C_RX_LINE_MAX_LEN);
  rxLineQueue.head  = (uint8_t)((rxLineQueue.head + 1U) % RX_QUEUE_SIZE);
  rxLineQueue.count--;
  return 1U;
}

static uint8_t __DRIVER_A7670C_WaitResp(waitType_e waitType, uint32_t timeoutMs)
{
  uint32_t startMs = HAL_GetTick();
  uint8_t  line[A7670C_RX_LINE_MAX_LEN];

  while ((HAL_GetTick() - startMs) < timeoutMs) {
    osDelay(1U);
    while (__DRIVER_A7670C_Dequeue(line)) {
      switch (waitType) {
        case WAIT_OK:
          if (strncmp((char *)line, "OK",         2U)  == 0) return 1U;
          if (strncmp((char *)line, "ERROR",       5U)  == 0) return 0U;
          if (strncmp((char *)line, "+CMS ERROR", 10U)  == 0) return 0U;
          break;
        case WAIT_PROMPT:
          if (line[0] == '>') return 1U;
          break;
        case WAIT_SEND_OK:
          if (strncmp((char *)line, "+CMGS:",      6U)  == 0) return 1U;
          if (strncmp((char *)line, "ERROR",       5U)  == 0) return 0U;
          if (strncmp((char *)line, "+CMS ERROR", 10U)  == 0) return 0U;
          break;
        case WAIT_LINE:
          if (strncmp((char *)line, "OK",          2U)  == 0) break;
          if (strncmp((char *)line, "ERROR",       5U)  == 0) return 0U;
          if (strncmp((char *)line, "+CMS ERROR", 10U)  == 0) return 0U;
          if (line[0] != '\0') {
            memcpy(a7670cInfo.rxLine, line, A7670C_RX_LINE_MAX_LEN);
            return 1U;
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
 * 自检状态机
 *============================================================================*/

static void __DRIVER_A7670C_RunDiag(void)
{
  diagState_e   diagState = DIAG_STATE_STEP1_SEND;
  a7670cDiag_t *d         = &a7670cInfo.diag;

  d->step1Result = A7670C_DIAG_PENDING;
  d->step2Result = A7670C_DIAG_PENDING;
  d->step3Result = A7670C_DIAG_PENDING;
  d->step4Result = A7670C_DIAG_PENDING;
  d->allPass     = 0U;
  d->csqRssi     = 99U;
  d->ceregStat   = 0xFFU;
  memset(d->imsi,     0, sizeof(d->imsi));
  memset(d->step1Raw, 0, sizeof(d->step1Raw));
  memset(d->step2Raw, 0, sizeof(d->step2Raw));
  memset(d->step3Raw, 0, sizeof(d->step3Raw));
  memset(d->step4Raw, 0, sizeof(d->step4Raw));

  while (diagState != DIAG_STATE_DONE) {
    switch (diagState) {

      /* STEP1: AT 握手 */
      case DIAG_STATE_STEP1_SEND:
        __DRIVER_A7670C_SendCmd("AT");
        diagState = DIAG_STATE_STEP1_WAIT;
        break;

      case DIAG_STATE_STEP1_WAIT:
        if (__DRIVER_A7670C_WaitResp(WAIT_OK, A7670C_TIMEOUT_MS)) {
          d->step1Result = A7670C_DIAG_PASS;
          strncpy(d->step1Raw, "OK", A7670C_DIAG_RAW_MAX_LEN - 1U);
          diagState = DIAG_STATE_STEP2_SEND;
        } else {
          d->step1Result = A7670C_DIAG_FAIL;
          strncpy(d->step1Raw, (char *)a7670cInfo.rxLine, A7670C_DIAG_RAW_MAX_LEN - 1U);
          d->step2Result = A7670C_DIAG_FAIL;
          d->step3Result = A7670C_DIAG_FAIL;
          d->step4Result = A7670C_DIAG_FAIL;
          diagState = DIAG_STATE_DONE;
        }
        break;

      /* STEP2: AT+CIMI 读取 IMSI */
      case DIAG_STATE_STEP2_SEND:
        osDelay(100U);
        __DRIVER_A7670C_SendCmd("AT+CIMI");
        diagState = DIAG_STATE_STEP2_WAIT;
        break;

      case DIAG_STATE_STEP2_WAIT: {
        if (__DRIVER_A7670C_WaitResp(WAIT_LINE, A7670C_TIMEOUT_MS)) {
          const char *line = (char *)a7670cInfo.rxLine;
          uint8_t     len  = (uint8_t)strlen(line);
          strncpy(d->step2Raw, line, A7670C_DIAG_RAW_MAX_LEN - 1U);
          uint8_t allDigits = ((len >= 14U) && (len <= 15U)) ? 1U : 0U;
          for (uint8_t i = 0U; allDigits && (i < len); i++) {
            if ((line[i] < '0') || (line[i] > '9')) allDigits = 0U;
          }
          if (allDigits) {
            d->step2Result = A7670C_DIAG_PASS;
            strncpy(d->imsi, line, A7670C_IMSI_MAX_LEN - 1U);
          } else {
            d->step2Result = A7670C_DIAG_FAIL;
          }
        } else {
          d->step2Result = A7670C_DIAG_FAIL;
          strncpy(d->step2Raw, (char *)a7670cInfo.rxLine, A7670C_DIAG_RAW_MAX_LEN - 1U);
        }
        diagState = DIAG_STATE_STEP3_SEND;
        break;
      }

      /* STEP3: AT+CSQ 信号强度 */
      case DIAG_STATE_STEP3_SEND:
        osDelay(100U);
        __DRIVER_A7670C_SendCmd("AT+CSQ");
        diagState = DIAG_STATE_STEP3_WAIT;
        break;

      case DIAG_STATE_STEP3_WAIT:
        if (__DRIVER_A7670C_WaitResp(WAIT_LINE, A7670C_TIMEOUT_MS)) {
          const char *line = (char *)a7670cInfo.rxLine;
          strncpy(d->step3Raw, line, A7670C_DIAG_RAW_MAX_LEN - 1U);
          if (strncmp(line, "+CSQ:", 5U) == 0) {
            const char *p = line + 5U;
            while (*p == ' ') p++;
            d->csqRssi     = (uint8_t)atoi(p);
            d->step3Result = A7670C_DIAG_PASS;
          } else {
            d->step3Result = A7670C_DIAG_FAIL;
          }
        } else {
          d->step3Result = A7670C_DIAG_FAIL;
          strncpy(d->step3Raw, (char *)a7670cInfo.rxLine, A7670C_DIAG_RAW_MAX_LEN - 1U);
        }
        diagState = DIAG_STATE_STEP4_SEND;
        break;

      /* STEP4: AT+CEREG? 4G 注册状态（较长超时） */
      case DIAG_STATE_STEP4_SEND:
        osDelay(100U);
        __DRIVER_A7670C_SendCmd("AT+CEREG?");
        diagState = DIAG_STATE_STEP4_WAIT;
        break;

      case DIAG_STATE_STEP4_WAIT:
        if (__DRIVER_A7670C_WaitResp(WAIT_LINE, A7670C_CEREG_TIMEOUT_MS)) {
          const char *line  = (char *)a7670cInfo.rxLine;
          strncpy(d->step4Raw, line, A7670C_DIAG_RAW_MAX_LEN - 1U);
          if (strncmp(line, "+CEREG:", 7U) == 0) {
            const char *p     = line + 7U;
            while (*p == ' ') p++;
            const char *comma = strchr(p, ',');
            d->ceregStat  = (uint8_t)atoi(comma ? (comma + 1U) : p);
            d->step4Result = ((d->ceregStat == 1U) || (d->ceregStat == 5U))
                             ? A7670C_DIAG_PASS : A7670C_DIAG_FAIL;
          } else {
            d->step4Result = A7670C_DIAG_FAIL;
          }
        } else {
          d->step4Result = A7670C_DIAG_FAIL;
          strncpy(d->step4Raw, (char *)a7670cInfo.rxLine, A7670C_DIAG_RAW_MAX_LEN - 1U);
        }
        diagState = DIAG_STATE_DONE;
        break;

      case DIAG_STATE_DONE:
      default:
        break;
    }
  }

  if (   (d->step1Result == A7670C_DIAG_PASS)
      && (d->step2Result == A7670C_DIAG_PASS)
      && (d->step3Result == A7670C_DIAG_PASS)
      && (d->step4Result == A7670C_DIAG_PASS)) {
    d->allPass = 1U;
  }
}

/*============================================================================
 * PDU 构建辅助
 *============================================================================*/

static void __DRIVER_A7670C_ByteToHex(uint8_t byte, char *out)
{
  static const char tab[16] = "0123456789ABCDEF";
  out[0] = tab[byte >> 4U];
  out[1] = tab[byte & 0x0FU];
}

static uint16_t __DRIVER_A7670C_Utf8ToUcs2(
    const char *utf8, uint8_t *out, uint16_t maxChars)
{
  const uint8_t *p       = (const uint8_t *)utf8;
  uint16_t       charCnt = 0U;
  uint16_t       ucs2;

  while ((*p != 0U) && (charCnt < maxChars)) {
    if (*p < 0x80U) {
      ucs2 = (uint16_t)*p++;
    } else if ((*p & 0xE0U) == 0xC0U) {
      if ((*(p + 1U) & 0xC0U) != 0x80U) { p++; continue; }
      ucs2 = (uint16_t)(((uint16_t)(*p & 0x1FU) << 6U) | (*(p + 1U) & 0x3FU));
      p += 2U;
    } else if ((*p & 0xF0U) == 0xE0U) {
      if (((*(p+1U) & 0xC0U) != 0x80U) || ((*(p+2U) & 0xC0U) != 0x80U)) { p++; continue; }
      ucs2 = (uint16_t)(((uint16_t)(*p & 0x0FU) << 12U) |
                        ((uint16_t)(*(p+1U) & 0x3FU) << 6U) |
                        (*(p+2U) & 0x3FU));
      p += 3U;
    } else {
      p++; continue;
    }
    out[charCnt * 2U]      = (uint8_t)(ucs2 >> 8U);
    out[charCnt * 2U + 1U] = (uint8_t)(ucs2 & 0xFFU);
    charCnt++;
  }
  return (uint16_t)(charCnt * 2U);
}

static uint16_t __DRIVER_A7670C_BuildPduHex(
    const char *phone, const uint8_t *ucs2, uint16_t ucs2Len,
    char *pduHex, uint16_t pduHexSize)
{
  const char *digits;
  uint8_t     daType, digitLen, bcdLen, i, d1, d2;
  uint16_t    hexIdx, needed;

  if (phone[0] == '+') { daType = 0x91U; digits = phone + 1U; }
  else                  { daType = 0x81U; digits = phone;      }

  digitLen = (uint8_t)strlen(digits);
  if ((digitLen == 0U) || (digitLen > A7670C_PHONE_MAX_DIGITS)) return 0U;
  bcdLen = (uint8_t)((digitLen + 1U) / 2U);

  needed = (uint16_t)(19U + (uint16_t)bcdLen * 2U + ucs2Len * 2U);
  if (needed > pduHexSize) return 0U;

  hexIdx = 0U;
  __DRIVER_A7670C_ByteToHex(0x00U,     &pduHex[hexIdx]); hexIdx += 2U;
  __DRIVER_A7670C_ByteToHex(0x11U,     &pduHex[hexIdx]); hexIdx += 2U;
  __DRIVER_A7670C_ByteToHex(0x00U,     &pduHex[hexIdx]); hexIdx += 2U;
  __DRIVER_A7670C_ByteToHex(digitLen,  &pduHex[hexIdx]); hexIdx += 2U;
  __DRIVER_A7670C_ByteToHex(daType,    &pduHex[hexIdx]); hexIdx += 2U;

  for (i = 0U; i < bcdLen; i++) {
    d1 = (uint8_t)(digits[(uint16_t)i * 2U] - '0');
    d2 = (((uint16_t)i * 2U + 1U) < (uint16_t)digitLen)
         ? (uint8_t)(digits[(uint16_t)i * 2U + 1U] - '0') : 0x0FU;
    __DRIVER_A7670C_ByteToHex((uint8_t)((d2 << 4U) | d1), &pduHex[hexIdx]);
    hexIdx += 2U;
  }

  __DRIVER_A7670C_ByteToHex(0x00U,            &pduHex[hexIdx]); hexIdx += 2U;
  __DRIVER_A7670C_ByteToHex(0x08U,            &pduHex[hexIdx]); hexIdx += 2U;
  __DRIVER_A7670C_ByteToHex(0xFFU,            &pduHex[hexIdx]); hexIdx += 2U;
  __DRIVER_A7670C_ByteToHex((uint8_t)ucs2Len, &pduHex[hexIdx]); hexIdx += 2U;

  for (i = 0U; i < (uint8_t)ucs2Len; i++) {
    __DRIVER_A7670C_ByteToHex(ucs2[i], &pduHex[hexIdx]);
    hexIdx += 2U;
  }
  pduHex[hexIdx] = '\0';

  return (uint16_t)(hexIdx / 2U - 1U);
}

/*============================================================================
 * 网络注册等待
 *============================================================================*/

/* 每 2s 查询一次 AT+CEREG?，最长等待 30s，注册成功返回 1，超时返回 0 */
static uint8_t __DRIVER_A7670C_WaitNetworkReady(void)
{
  uint32_t startMs = HAL_GetTick();

  while ((HAL_GetTick() - startMs) < A7670C_CEREG_TIMEOUT_MS) {
    __DRIVER_A7670C_SendCmd("AT+CEREG?");
    if (__DRIVER_A7670C_WaitResp(WAIT_LINE, 2000U)) {
      const char *p    = strchr((char *)a7670cInfo.rxLine, ',');
      uint8_t     stat = (p != NULL) ? (uint8_t)atoi(p + 1U) : 0U;
      if ((stat == 1U) || (stat == 5U)) return 1U;
    }
    osDelay(2000U);
  }
  return 0U;
}

/*============================================================================
 * API
 *============================================================================*/

/*
 * DRIVER_A7670C_Init
 *
 * 初始化驱动：
 *   1. 注册串口字节回调。
 *   2. 等待模块输出 "SMS DONE"（最长 10s）。
 *   3. 执行 4 步自检（AT / CIMI / CSQ / CEREG），结果存入 a7670cInfo.diag。
 *
 * 阻塞总时长：约 10s（启动等待）+ 最长 45s（自检）。
 *
 * 调试时在 Watch 窗口查看 a7670cInfo.diag：
 *   allPass=1    → 完全就绪
 *   stepXResult  → PASS(1) / FAIL(0) / PENDING(0xFF)
 *   stepXRaw     → 模块原始回复字符串
 *   imsi         → SIM 卡 IMSI（15 位数字）
 *   csqRssi      → 信号强度（99=无天线，<10=极弱）
 *   ceregStat    → 注册状态（1/5=成功，2=搜网中，6=物联网卡）
 */
void DRIVER_A7670C_Init(void)
{
  memset(&a7670cInfo, 0, sizeof(a7670cInfo));
  memset(&rxLineQueue, 0, sizeof(rxLineQueue));
  rxIdx = 0U;
  memset(rxBuf, 0, sizeof(rxBuf));

  A7670C_DEP_UART_SET_CUSTOM_CB(__DRIVER_A7670C_UartByteCallback);

  /* 等待 "SMS DONE" URC */
  uint32_t startMs = HAL_GetTick();
  while ((HAL_GetTick() - startMs) < A7670C_BOOT_TIMEOUT_MS) {
    osDelay(1U);
    if (a7670cInfo.rxLineReady) {
      a7670cInfo.rxLineReady = 0U;
      if (strncmp((char *)a7670cInfo.rxLine, "SMS DONE", 8U) == 0) break;
    }
  }
  osDelay(500U);
	
	/* 关闭指令回显，避免回显行干扰响应解析 */
	__DRIVER_A7670C_SendCmd("ATE0");
	__DRIVER_A7670C_WaitResp(WAIT_OK, A7670C_TIMEOUT_MS);
  /* 执行自检 */
  __DRIVER_A7670C_RunDiag();
}

/*
 * DRIVER_A7670C_SendSms
 *
 * 发送短信（PDU 模式，UCS2 编码，支持中文）。
 *   phone   ：目标手机号（如 "13800138000" 或 "+8613800138000"）
 *   content ：短信内容（UTF-8，最多 70 字符）
 *   返回值  ：A7670C_SMS_OK(1)=成功，A7670C_SMS_FAIL(0)=失败
 *
 * 发送流程：
 *   0. 等待 4G 网络注册完成（AT+CEREG? 轮询，最长 30s）
 *   1. UTF-8 → UCS2 大端字节序列
 *   2. 构建 PDU 十六进制串
 *   3. AT+CMGF=0（切换 PDU 模式）
 *   4. AT+CMGS=<len>，等待 ">"
 *   5. 发送 PDU + Ctrl+Z 提交
 *   6. 等待 "+CMGS:" 确认
 */
uint8_t DRIVER_A7670C_SendSms(const char *phone, const char *content)
{
  static uint8_t ucs2Bytes[A7670C_UCS2_MAX_CHARS * 2U];
  static char    pduHex[A7670C_PDU_HEX_MAX_LEN];
  static char    cmdBuf[A7670C_CMD_BUF_LEN];
  static uint8_t ctrlZ[1] = {A7670C_CTRL_Z};
  uint16_t       ucs2Len;
  uint16_t       pduLen;

  if ((phone == NULL) || (content == NULL)) return A7670C_SMS_FAIL;

  a7670cInfo.state = A7670C_STATE_BUSY;

  /* 步骤0：等待 4G 网络注册 */
  if (__DRIVER_A7670C_WaitNetworkReady() == 0U) {
    a7670cInfo.state         = A7670C_STATE_IDLE;
    a7670cInfo.lastSmsResult = A7670C_SMS_FAIL;
    return A7670C_SMS_FAIL;
  }

  /* 步骤1：UTF-8 → UCS2 */
  ucs2Len = __DRIVER_A7670C_Utf8ToUcs2(content, ucs2Bytes, A7670C_UCS2_MAX_CHARS);
  if (ucs2Len == 0U) {
    a7670cInfo.state         = A7670C_STATE_IDLE;
    a7670cInfo.lastSmsResult = A7670C_SMS_FAIL;
    return A7670C_SMS_FAIL;
  }

  /* 步骤2：构建 PDU */
  pduLen = __DRIVER_A7670C_BuildPduHex(phone, ucs2Bytes, ucs2Len,
                                        pduHex, A7670C_PDU_HEX_MAX_LEN);
  if (pduLen == 0U) {
    a7670cInfo.state         = A7670C_STATE_IDLE;
    a7670cInfo.lastSmsResult = A7670C_SMS_FAIL;
    return A7670C_SMS_FAIL;
  }

  /* 步骤3：PDU 模式 */
  __DRIVER_A7670C_SendCmd("AT+CMGF=0");
  if (__DRIVER_A7670C_WaitResp(WAIT_OK, A7670C_TIMEOUT_MS) == 0U) {
    a7670cInfo.state         = A7670C_STATE_IDLE;
    a7670cInfo.lastSmsResult = A7670C_SMS_FAIL;
    return A7670C_SMS_FAIL;
  }

  /* 步骤4：AT+CMGS，等待 ">" */
  snprintf(cmdBuf, A7670C_CMD_BUF_LEN, "AT+CMGS=%u", (unsigned int)pduLen);
  __DRIVER_A7670C_SendCmd(cmdBuf);
  if (__DRIVER_A7670C_WaitResp(WAIT_PROMPT, A7670C_TIMEOUT_MS) == 0U) {
    A7670C_DEP_UART_SEND_RAW(ctrlZ, 1U);
    a7670cInfo.state         = A7670C_STATE_IDLE;
    a7670cInfo.lastSmsResult = A7670C_SMS_FAIL;
    return A7670C_SMS_FAIL;
  }

  /* 步骤5：发送 PDU + Ctrl+Z */
  __DRIVER_A7670C_SendStr(pduHex);
  A7670C_DEP_UART_SEND_RAW(ctrlZ, 1U);

  /* 步骤6：等待 "+CMGS:" */
  if (__DRIVER_A7670C_WaitResp(WAIT_SEND_OK, A7670C_TIMEOUT_MS) == 0U) {
    a7670cInfo.state         = A7670C_STATE_IDLE;
    a7670cInfo.lastSmsResult = A7670C_SMS_FAIL;
    return A7670C_SMS_FAIL;
  }

  a7670cInfo.state         = A7670C_STATE_IDLE;
  a7670cInfo.lastSmsResult = A7670C_SMS_OK;
  return A7670C_SMS_OK;
}
