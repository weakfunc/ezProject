/*============================================================================
 * README
 * driver_a7670c：A7670C 4G 模块驱动
 * 通过 USART1 发送 AT 指令，实现 PDU 模式短信发送功能（支持中文 UCS2 编码）。
 * UART 接收使用中断+静态环形缓冲区（由 stdlib_usart 层管理），本模块注册字节回调
 * 逐字节拼接 AT 响应行，供阻塞式发送接口轮询。
 *============================================================================*/

#include "driver_a7670c.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* AT 指令等待响应超时时间（毫秒），可按需修改，默认 5000ms。 */
#define A7670C_TIMEOUT_MS         (5000U)

/* 单条短信 UCS2 最大字符数（UCS2 编码单条短信上限为 70 字符）。 */
#define A7670C_UCS2_MAX_CHARS     (70U)

/* 手机号码最大位数（不含 '+' 前缀，不含 '\0'）。 */
#define A7670C_PHONE_MAX_DIGITS   (20U)

/* PDU 十六进制字符串最大长度（含末尾 '\0'）。
 * 最坏情况：SMSC(2)+TYPE(2)+MR(2)+DA_LEN(2)+DA_TYPE(2)+DA_BCD(20)
 *           +PID(2)+DCS(2)+UDL(2)+UD(280) = 316，取 320 留裕量。
 */
#define A7670C_PDU_HEX_MAX_LEN    (320U)

/* AT+CMGS=xxx 命令缓冲区长度："AT+CMGS=" + 最多 3 位数字 + '\0'。 */
#define A7670C_CMD_BUF_LEN        (16U)

/* Ctrl+Z（0x1A），用于结束短信内容输入。 */
#define A7670C_CTRL_Z             (0x1AU)

/*============================================================================
 * 私有类型定义
 *============================================================================*/

/* AT 指令等待的响应类型。 */
typedef enum {
  WAIT_OK,       /* 等待 "OK" */
  WAIT_PROMPT,   /* 等待 ">" 提示符 */
  WAIT_SEND_OK,  /* 等待 "+CMGS:" 发送成功回应 */
} waitType_e;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* A7670C 公有信息结构体（extern 在 .h 中声明）。 */
a7670cInfo_t a7670cInfo;

/* 接收行拼接缓冲区（私有）。 */
static uint8_t rxBuf[A7670C_RX_LINE_MAX_LEN];

/* 当前接收缓冲区写入索引。 */
static uint8_t rxIdx = 0U;

/*============================================================================
 * 私有函数
 *============================================================================*/

/* USART1 自定义字节回调：逐字节拼接 AT 响应行。
 * 以 '\n' 作为行结束，将完整行复制到 a7670cInfo.rxLine 并置位就绪标志。
 * '>' 提示符无换行，单独作为一行触发。
 */
static void __DRIVER_A7670C_UartByteCallback(uint8_t port, uint8_t byte){
  if(port != A7670C_DEP_UART_PORT) return;

  /* '>' 提示符无换行，单独触发行就绪 */
  if(byte == '>'){
    rxBuf[0] = '>';
    rxBuf[1] = '\0';
    memcpy(a7670cInfo.rxLine, rxBuf, 2U);
    a7670cInfo.rxLineReady = 1U;
    rxIdx = 0U;
    return;
  }

  /* 忽略 '\r'，保持缓冲区内容为纯文本 */
  if(byte == '\r') return;

  if(byte == '\n'){
    /* 行结束：将非空缓冲内容发布为就绪行，空行直接丢弃 */
    if(rxIdx > 0U){
      rxBuf[rxIdx] = '\0';
      memcpy(a7670cInfo.rxLine, rxBuf,
             (rxIdx + 1U < A7670C_RX_LINE_MAX_LEN) ? (rxIdx + 1U) : A7670C_RX_LINE_MAX_LEN);
      a7670cInfo.rxLine[A7670C_RX_LINE_MAX_LEN - 1U] = '\0';
      a7670cInfo.rxLineReady = 1U;
    }
    rxIdx = 0U;
    return;
  }

  /* 普通字符：追加到缓冲区，防止溢出 */
  if(rxIdx < (A7670C_RX_LINE_MAX_LEN - 1U)){
    rxBuf[rxIdx++] = byte;
  }
}

/* 发送原始字符串（不追加换行）。 */
static void __DRIVER_A7670C_SendStr(const char *str){
  A7670C_DEP_UART_SEND_RAW((uint8_t *)str, (uint16_t)strlen(str));
}

/* 发送 AT 指令行（自动追加 "\r\n"）。 */
static void __DRIVER_A7670C_SendCmd(const char *cmd){
  static const uint8_t crlf[2] = {'\r', '\n'};
  __DRIVER_A7670C_SendStr(cmd);
  A7670C_DEP_UART_SEND_RAW((uint8_t *)crlf, 2U);
}

/* 等待指定类型响应，超时返回 0，匹配成功返回 1。
 * 依赖 rxLineReady 标志，由 STDLIB_USART_Updata() 在系统任务中周期性调用驱动回调写入。
 */
static uint8_t __DRIVER_A7670C_WaitResp(waitType_e waitType, uint32_t timeoutMs){
  uint32_t startMs = HAL_GetTick();

  while((HAL_GetTick() - startMs) < timeoutMs){
    osDelay(1U); /* 让出 CPU，避免忙等期间饿死其他任务 */
    if(a7670cInfo.rxLineReady){
      a7670cInfo.rxLineReady = 0U;
      switch(waitType){
        case WAIT_OK:
          if(strncmp((char *)a7670cInfo.rxLine, "OK", 2U) == 0){
            return 1U;
          }
          /* 收到 "ERROR" 或 "+CMS ERROR" 则提前退出 */
          if(strncmp((char *)a7670cInfo.rxLine, "ERROR", 5U) == 0){
            return 0U;
          }
          if(strncmp((char *)a7670cInfo.rxLine, "+CMS ERROR", 10U) == 0){
            return 0U;
          }
          break;

        case WAIT_PROMPT:
          if(a7670cInfo.rxLine[0] == '>'){
            return 1U;
          }
          break;

        case WAIT_SEND_OK:
          /* "+CMGS:" 开头表示短信已提交至网络 */
          if(strncmp((char *)a7670cInfo.rxLine, "+CMGS:", 6U) == 0){
            return 1U;
          }
          if(strncmp((char *)a7670cInfo.rxLine, "ERROR", 5U) == 0){
            return 0U;
          }
          if(strncmp((char *)a7670cInfo.rxLine, "+CMS ERROR", 10U) == 0){
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

/* 将单字节转为两个大写十六进制 ASCII 字符，写入 out[0] 和 out[1]。 */
static void __DRIVER_A7670C_ByteToHex(uint8_t byte, char *out){
  static const char hexTab[16] = {
    '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
  };
  out[0] = hexTab[byte >> 4U];
  out[1] = hexTab[byte & 0x0FU];
}

/* 将 UTF-8 字符串转为 UCS2 大端字节数组。
 * utf8    ：输入 UTF-8 字符串（以 '\0' 结尾）。
 * out     ：输出 UCS2 字节数组（每字符 2 字节，大端序），缓冲区至少 maxChars*2 字节。
 * maxChars：最多转换的字符数。
 * 返回值  ：实际写入 out 的字节数（= 实际字符数 * 2）。
 */
static uint16_t __DRIVER_A7670C_Utf8ToUcs2(
    const char *utf8, uint8_t *out, uint16_t maxChars)
{
  const uint8_t *p = (const uint8_t *)utf8;
  uint16_t charCnt = 0U;
  uint16_t ucs2;

  while((*p != 0U) && (charCnt < maxChars)){
    if(*p < 0x80U){
      /* 单字节 ASCII（0x00~0x7F）：直接映射为 UCS2 */
      ucs2 = (uint16_t)*p;
      p++;
    } else if((*p & 0xE0U) == 0xC0U){
      /* 双字节 UTF-8：110xxxxx 10xxxxxx（UCS2 0x0080~0x07FF） */
      if((*(p + 1U) & 0xC0U) != 0x80U){ p++; continue; }
      ucs2 = (uint16_t)(((uint16_t)(*p & 0x1FU) << 6U) |
                        (uint16_t)(*(p + 1U) & 0x3FU));
      p += 2U;
    } else if((*p & 0xF0U) == 0xE0U){
      /* 三字节 UTF-8：1110xxxx 10xxxxxx 10xxxxxx（UCS2 0x0800~0xFFFF，覆盖 BMP 中文） */
      if(((*(p + 1U) & 0xC0U) != 0x80U) || ((*(p + 2U) & 0xC0U) != 0x80U)){
        p++;
        continue;
      }
      ucs2 = (uint16_t)(((uint16_t)(*p & 0x0FU) << 12U) |
                        ((uint16_t)(*(p + 1U) & 0x3FU) << 6U) |
                        (uint16_t)(*(p + 2U) & 0x3FU));
      p += 3U;
    } else {
      /* 不支持的编码（4 字节 UTF-8 超出 BMP）：跳过当前字节 */
      p++;
      continue;
    }
    /* 大端序写入 UCS2 字节对 */
    out[charCnt * 2U]      = (uint8_t)(ucs2 >> 8U);
    out[charCnt * 2U + 1U] = (uint8_t)(ucs2 & 0xFFU);
    charCnt++;
  }
  return (uint16_t)(charCnt * 2U);
}

/* 根据手机号和 UCS2 内容构建 PDU 十六进制字符串，写入 pduHex。
 * PDU 结构（SMS-SUBMIT，短信中心号码留空，UCS2 编码，含相对有效期字段）：
 *   00        SMSC 长度 = 0（使用 SIM 卡默认短信中心号码）
 *   11        PDU-TYPE（0x11：SMS-SUBMIT，VPF=10 相对有效期格式）
 *   00        MR（消息参考值）
 *   DA_LEN    目的地址位数
 *   DA_TYPE   地址类型（0x81 国内，0x91 国际）
 *   DA_BCD    手机号 BCD 编码（低 4 位存奇位，高 4 位存偶位，奇数位末尾补 F）
 *   00        PID
 *   08        DCS（UCS2 编码）
 *   FF        VP（相对有效期，0xFF = 约 63 周，网络兼容性最优值）
 *   UDL       用户数据字节数
 *   UD        UCS2 字节流（大端序）
 *
 * phone     ：目标手机号（纯数字，或以 '+' 开头的国际格式）。
 * ucs2      ：UCS2 字节数组（大端序）。
 * ucs2Len   ：UCS2 字节数（字符数 * 2）。
 * pduHex    ：输出 PDU 十六进制字符串缓冲区（含 '\0'）。
 * pduHexSize：pduHex 缓冲区字节数。
 * 返回值    ：AT+CMGS 参数值（PDU 字节数不含 SMSC 字节），0 表示出错。
 */
static uint16_t __DRIVER_A7670C_BuildPduHex(
    const char *phone, const uint8_t *ucs2, uint16_t ucs2Len,
    char *pduHex, uint16_t pduHexSize)
{
  const char *digits;
  uint8_t     daType;
  uint8_t     digitLen;
  uint8_t     bcdLen;
  uint16_t    hexIdx;
  uint16_t    needed;
  uint8_t     i;
  uint8_t     d1;
  uint8_t     d2;

  /* 判断号码类型并提取纯数字部分 */
  if(phone[0] == '+'){
    daType = 0x91U; /* 国际格式 */
    digits = phone + 1U;
  } else {
    daType = 0x81U; /* 国内格式 */
    digits = phone;
  }

  digitLen = (uint8_t)strlen(digits);
  if((digitLen == 0U) || (digitLen > A7670C_PHONE_MAX_DIGITS)) return 0U;
  bcdLen = (uint8_t)((digitLen + 1U) / 2U);

  /* 检查缓冲区容量：所有字段十六进制字符数 + '\0'
   * = SMSC(2)+TYPE(2)+MR(2)+DA_LEN(2)+DA_TYPE(2)+DA_BCD(bcdLen*2)
   *   +PID(2)+DCS(2)+VP(2)+UDL(2)+UD(ucs2Len*2)+'\0'
   * = 19 + bcdLen*2 + ucs2Len*2
   */
  needed = (uint16_t)(19U + (uint16_t)bcdLen * 2U + ucs2Len * 2U);
  if(needed > pduHexSize) return 0U;

  hexIdx = 0U;

  /* 1. SMSC 长度：00（使用 SIM 卡默认短信中心号码） */
  __DRIVER_A7670C_ByteToHex(0x00U, &pduHex[hexIdx]); hexIdx += 2U;
  /* 2. PDU-TYPE：0x11（SMS-SUBMIT，VPF=10 含相对有效期字段） */
  __DRIVER_A7670C_ByteToHex(0x11U, &pduHex[hexIdx]); hexIdx += 2U;
  /* 3. MR：消息参考值，0x00 */
  __DRIVER_A7670C_ByteToHex(0x00U, &pduHex[hexIdx]); hexIdx += 2U;
  /* 4. DA-LEN：目的地址位数 */
  __DRIVER_A7670C_ByteToHex(digitLen, &pduHex[hexIdx]); hexIdx += 2U;
  /* 5. DA-TYPE：地址类型 */
  __DRIVER_A7670C_ByteToHex(daType, &pduHex[hexIdx]); hexIdx += 2U;

  /* 6. DA-BCD：手机号 BCD 编码（低 4 位存奇位数字，高 4 位存偶位数字） */
  for(i = 0U; i < bcdLen; i++){
    d1 = (uint8_t)(digits[(uint16_t)i * 2U] - '0');
    if(((uint16_t)i * 2U + 1U) < (uint16_t)digitLen){
      d2 = (uint8_t)(digits[(uint16_t)i * 2U + 1U] - '0');
    } else {
      d2 = 0x0FU; /* 奇数位末尾补 F */
    }
    __DRIVER_A7670C_ByteToHex((uint8_t)((d2 << 4U) | d1), &pduHex[hexIdx]);
    hexIdx += 2U;
  }

  /* 7. PID：0x00 */
  __DRIVER_A7670C_ByteToHex(0x00U, &pduHex[hexIdx]); hexIdx += 2U;
  /* 8. DCS：0x08（UCS2 编码） */
  __DRIVER_A7670C_ByteToHex(0x08U, &pduHex[hexIdx]); hexIdx += 2U;
  /* 9. VP：0xFF（相对有效期最大值，约 63 周，与 PDU-TYPE=0x11 的 VPF=10 对应） */
  __DRIVER_A7670C_ByteToHex(0xFFU, &pduHex[hexIdx]); hexIdx += 2U;
  /* 10. UDL：用户数据字节数 */
  __DRIVER_A7670C_ByteToHex((uint8_t)ucs2Len, &pduHex[hexIdx]); hexIdx += 2U;

  /* 11. UD：UCS2 字节流 */
  for(i = 0U; i < (uint8_t)ucs2Len; i++){
    __DRIVER_A7670C_ByteToHex(ucs2[i], &pduHex[hexIdx]);
    hexIdx += 2U;
  }

  pduHex[hexIdx] = '\0';

  /* AT+CMGS 参数 = PDU 总字节数 - 1（扣除 SMSC 的 1 字节） */
  return (uint16_t)(hexIdx / 2U - 1U);
}

/*============================================================================
 * API 接口
 *============================================================================*/

/* 初始化 A7670C 驱动，注册 USART1 字节回调。 */
void DRIVER_A7670C_Init(void){
  memset(&a7670cInfo, 0, sizeof(a7670cInfo));
  rxIdx = 0U;
  memset(rxBuf, 0, sizeof(rxBuf));
  A7670C_DEP_UART_SET_CUSTOM_CB(__DRIVER_A7670C_UartByteCallback);
}

/* 发送短信（PDU 模式，UCS2 编码，支持中文）。
 * phone   ：目标手机号字符串（如 "13800138000" 或 "+8613800138000"）。
 * content ：短信内容字符串（UTF-8 编码，支持中文，最多 70 个字符）。
 * 返回值  ：A7670C_SMS_OK（1）= 成功，A7670C_SMS_FAIL（0）= 失败。
 * 注意    ：本函数为阻塞调用，等待模块完整响应，请勿在中断中调用。
 */
uint8_t DRIVER_A7670C_SendSms(const char *phone, const char *content){
  /* UCS2 字节缓冲区：最多 70 字符 * 2 字节 = 140 字节 */
  static uint8_t ucs2Bytes[A7670C_UCS2_MAX_CHARS * 2U];
  /* PDU 十六进制字符串缓冲区 */
  static char    pduHex[A7670C_PDU_HEX_MAX_LEN];
  /* AT+CMGS=xxx 命令缓冲区 */
  static char    cmdBuf[A7670C_CMD_BUF_LEN];
  /* Ctrl+Z 发送缓冲 */
  static uint8_t ctrlZ[1] = {A7670C_CTRL_Z};

  uint16_t ucs2Len;
  uint16_t pduLen;

  if((phone == NULL) || (content == NULL)){
    return A7670C_SMS_FAIL;
  }

  a7670cInfo.state = A7670C_STATE_BUSY;

  /* 步骤1：将 UTF-8 内容转为 UCS2 大端字节序列 */
  ucs2Len = __DRIVER_A7670C_Utf8ToUcs2(content, ucs2Bytes, A7670C_UCS2_MAX_CHARS);
  if(ucs2Len == 0U){
    a7670cInfo.state         = A7670C_STATE_IDLE;
    a7670cInfo.lastSmsResult = A7670C_SMS_FAIL;
    return A7670C_SMS_FAIL;
  }

  /* 步骤2：构建 PDU 十六进制字符串 */
  pduLen = __DRIVER_A7670C_BuildPduHex(phone, ucs2Bytes, ucs2Len,
                                        pduHex, A7670C_PDU_HEX_MAX_LEN);
  if(pduLen == 0U){
    a7670cInfo.state         = A7670C_STATE_IDLE;
    a7670cInfo.lastSmsResult = A7670C_SMS_FAIL;
    return A7670C_SMS_FAIL;
  }

  /* 步骤3：设置 PDU 模式（AT+CMGF=0） */
  __DRIVER_A7670C_SendCmd("AT+CMGF=0");
  if(__DRIVER_A7670C_WaitResp(WAIT_OK, A7670C_TIMEOUT_MS) == 0U){
    a7670cInfo.state         = A7670C_STATE_IDLE;
    a7670cInfo.lastSmsResult = A7670C_SMS_FAIL;
    return A7670C_SMS_FAIL;
  }

  /* 步骤4：发送 AT+CMGS=<pduLen>，等待 ">" 提示符 */
  snprintf(cmdBuf, A7670C_CMD_BUF_LEN, "AT+CMGS=%u", (unsigned int)pduLen);
  __DRIVER_A7670C_SendCmd(cmdBuf);
  if(__DRIVER_A7670C_WaitResp(WAIT_PROMPT, A7670C_TIMEOUT_MS) == 0U){
    /* 等待提示符超时，发送 Ctrl+Z 中止，恢复模块状态 */
    A7670C_DEP_UART_SEND_RAW(ctrlZ, 1U);
    a7670cInfo.state         = A7670C_STATE_IDLE;
    a7670cInfo.lastSmsResult = A7670C_SMS_FAIL;
    return A7670C_SMS_FAIL;
  }

  /* 步骤5：发送 PDU 十六进制字符串，末尾发送 Ctrl+Z 提交 */
  __DRIVER_A7670C_SendStr(pduHex);
  A7670C_DEP_UART_SEND_RAW(ctrlZ, 1U);

  /* 步骤6：等待 "+CMGS:" 确认短信已提交至网络 */
  if(__DRIVER_A7670C_WaitResp(WAIT_SEND_OK, A7670C_TIMEOUT_MS) == 0U){
    a7670cInfo.state         = A7670C_STATE_IDLE;
    a7670cInfo.lastSmsResult = A7670C_SMS_FAIL;
    return A7670C_SMS_FAIL;
  }

  a7670cInfo.state         = A7670C_STATE_IDLE;
  a7670cInfo.lastSmsResult = A7670C_SMS_OK;
  return A7670C_SMS_OK;
}
