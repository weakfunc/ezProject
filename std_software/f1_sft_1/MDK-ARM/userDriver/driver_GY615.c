/*============================================================================
 * README
 *
 *============================================================================*/

#include "driver_GY615.h"
#include <string.h>

GY615Info_t GY615Info;

/*
  TASKInit中调用：
    DRIVER_GY615_Init();
    DRIVER_GY615_Request();

  TASK中调用：
    DRIVER_GY615_GetInfo(&GY615Info);

  设置波特率为9600，温度信息在GY615Info中被更新
*/

/*============================================================================
 * 内部配置（仅 driver_GY615 模块内部使用）
 *============================================================================*/

/* 读温度数据起始寄存器：0x07（发射率），连续读7个（E/TO_H/TO_L/TA_H/TA_L/BO_H/BO_L） */
#define GY615_READ_START_REG    (0x07U)
/* 一次请求读取的寄存器数量 */
#define GY615_READ_COUNT        (0x07U)
/* 读寄存器功能码 */
#define GY615_FUNC_READ         (0x03U)
/* 内部接收缓冲区大小（需能容纳 GY615_READ_COUNT 个数据字节） */
#define GY615_DATA_BUF_SIZE     (16U)

/* 解析状态枚举 */
typedef enum {
    GY615_PARSE_WAIT_ADDR = 0, /* 等待器件地址字节 */
    GY615_PARSE_WAIT_FUNC,     /* 等待功能码字节 */
    GY615_PARSE_WAIT_REG,      /* 等待起始寄存器字节 */
    GY615_PARSE_WAIT_CNT,      /* 等待数据字节数 */
    GY615_PARSE_RECV_DATA,     /* 接收数据字节 */
    GY615_PARSE_WAIT_CSUM,     /* 等待校验和字节 */
} gy615ParseState_e;

/* GY615 模块运行时上下文（以模块名命名的内部结构体） */
typedef struct {
    gy615ParseState_e parseState;              /* 当前解析状态 */
    uint8_t           parseDataBuf[GY615_DATA_BUF_SIZE]; /* 数据字节缓冲 */
    uint8_t           parseDataIdx;            /* 已接收的数据字节数 */
    uint8_t           parseCount;              /* 本帧期望接收的数据字节总数 */
    uint8_t           parseReg;               /* 本帧起始寄存器号 */
    uint16_t          parseChecksum;           /* 累计校验和（低8位有效） */
    GY615Info_t       infoCache;              /* 最新解析数据缓存 */
} gy615Ctx_t;

/* GY615 模块静态上下文实例 */
static gy615Ctx_t gy615Ctx;

/* 复位解析状态机 */
static void __DRIVER_GY615_ResetParser(void){
  gy615Ctx.parseState    = GY615_PARSE_WAIT_ADDR;
  gy615Ctx.parseDataIdx  = 0U;
  gy615Ctx.parseCount    = 0U;
  gy615Ctx.parseReg      = 0U;
  gy615Ctx.parseChecksum = 0U;
}

/* 解析已接收的数据字节，更新 infoCache */
static void __DRIVER_GY615_ParseData(void){
  uint16_t rawTo;
  uint16_t rawTa;
  uint16_t rawBo;

  /* 数据布局（读取 GY615_READ_COUNT = 7 个寄存器，起始 0x07）：
   *   Buf[0] = E（发射率）
   *   Buf[1] = TO_H，Buf[2] = TO_L（目标温度高低字节）
   *   Buf[3] = TA_H，Buf[4] = TA_L（环境温度高低字节）
   *   Buf[5] = BO_H，Buf[6] = BO_L（额头体温高低字节）
   * 仅处理从寄存器 0x07 开始、数量为 7 的标准响应 */
  if((gy615Ctx.parseReg != GY615_READ_START_REG) ||
     (gy615Ctx.parseCount != GY615_READ_COUNT)){
    return;
  }

  gy615Ctx.infoCache.emissivity = gy615Ctx.parseDataBuf[0];

  rawTo = ((uint16_t)gy615Ctx.parseDataBuf[1] << 8U) | (uint16_t)gy615Ctx.parseDataBuf[2];
  rawTa = ((uint16_t)gy615Ctx.parseDataBuf[3] << 8U) | (uint16_t)gy615Ctx.parseDataBuf[4];
  rawBo = ((uint16_t)gy615Ctx.parseDataBuf[5] << 8U) | (uint16_t)gy615Ctx.parseDataBuf[6];

  /* 温度换算：原始值 / 100.0 即为摄氏度 */
  gy615Ctx.infoCache.targetTempC  = (float)rawTo / 100.0f;
  gy615Ctx.infoCache.ambientTempC = (float)rawTa / 100.0f;
  gy615Ctx.infoCache.bodyTempC    = (float)rawBo / 100.0f;
  gy615Ctx.infoCache.hasNewData   = 1U;
}

/* UART3 自定义字节回调：逐字节解析 GY615 读寄存器响应帧
 *
 * 帧格式（读响应）：
 *   [DevAddr][FuncCode][StartReg][Count][Data×Count][ChecksumLow8]
 * 校验和 = 前 (4 + Count) 个字节之和的低 8 位 */
static void __DRIVER_GY615_UartByteCallback(uint8_t port, uint8_t byte){
  if(port != GY615_DEP_UART_PORT) return;

  switch(gy615Ctx.parseState){

    case GY615_PARSE_WAIT_ADDR:
      if(byte == GY615_DEV_ADDR){
        gy615Ctx.parseChecksum = (uint16_t)byte;
        gy615Ctx.parseState    = GY615_PARSE_WAIT_FUNC;
      }
      break;

    case GY615_PARSE_WAIT_FUNC:
      if(byte == GY615_FUNC_READ){
        gy615Ctx.parseChecksum += (uint16_t)byte;
        gy615Ctx.parseState     = GY615_PARSE_WAIT_REG;
      } else {
        __DRIVER_GY615_ResetParser();
      }
      break;

    case GY615_PARSE_WAIT_REG:
      gy615Ctx.parseReg      = byte;
      gy615Ctx.parseChecksum += (uint16_t)byte;
      gy615Ctx.parseState    = GY615_PARSE_WAIT_CNT;
      break;

    case GY615_PARSE_WAIT_CNT:
      /* 数据字节数超出缓冲区则丢弃本帧 */
      if(byte > GY615_DATA_BUF_SIZE){
        __DRIVER_GY615_ResetParser();
        break;
      }
      gy615Ctx.parseCount    = byte;
      gy615Ctx.parseChecksum += (uint16_t)byte;
      gy615Ctx.parseDataIdx  = 0U;
      if(byte == 0U){
        /* 0个数据字节，直接等待校验和 */
        gy615Ctx.parseState = GY615_PARSE_WAIT_CSUM;
      } else {
        gy615Ctx.parseState = GY615_PARSE_RECV_DATA;
      }
      break;

    case GY615_PARSE_RECV_DATA:
      gy615Ctx.parseDataBuf[gy615Ctx.parseDataIdx] = byte;
      gy615Ctx.parseDataIdx++;
      gy615Ctx.parseChecksum += (uint16_t)byte;
      if(gy615Ctx.parseDataIdx >= gy615Ctx.parseCount){
        gy615Ctx.parseState = GY615_PARSE_WAIT_CSUM;
      }
      break;

    case GY615_PARSE_WAIT_CSUM:
      if((uint8_t)(gy615Ctx.parseChecksum & 0xFFU) == byte){
        /* 校验通过，解析数据 */
        __DRIVER_GY615_ParseData();
      }
      /* 无论校验是否通过，均复位状态机，准备接收下一帧 */
      __DRIVER_GY615_ResetParser();
      break;

    default:
      __DRIVER_GY615_ResetParser();
      break;
  }
}

/*============================================================================
 * API 接口
 *============================================================================*/

/* 初始化 GY615 驱动，清零上下文并注册串口接收回调 */
void DRIVER_GY615_Init(void){
  memset(&gy615Ctx, 0, sizeof(gy615Ctx));
  __DRIVER_GY615_ResetParser();
  GY615_DEP_UART_SET_CUSTOM_CB(__DRIVER_GY615_UartByteCallback);
}

/* 向 GY615 发送读温度寄存器请求帧（查询寄存器 0x07~0x0D，共 7 个）
 * 模块响应时间约 10ms（波特率 9600 默认），调用方需等待足够时间后再调用 GetInfo */
void DRIVER_GY615_Request(void){
  uint8_t frame[5];
  uint16_t checksum;

  frame[0] = GY615_DEV_ADDR;
  frame[1] = GY615_FUNC_READ;
  frame[2] = GY615_READ_START_REG;
  frame[3] = GY615_READ_COUNT;
  checksum  = (uint16_t)frame[0] + (uint16_t)frame[1] +
              (uint16_t)frame[2] + (uint16_t)frame[3];
  frame[4] = (uint8_t)(checksum & 0xFFU);

  GY615_DEP_UART_SEND_RAW(frame, (uint16_t)sizeof(frame));
}

/* 获取最新测温数据快照
 * 参数：info - 输出结构体指针（不可为 NULL）
 * 返回：1=有新数据并已拷贝，0=无新数据或参数无效 */
uint8_t DRIVER_GY615_GetInfo(GY615Info_t *info){
  if(info == NULL) return 0U;
  if(gy615Ctx.infoCache.hasNewData == 0U) return 0U;

  *info = gy615Ctx.infoCache;
  gy615Ctx.infoCache.hasNewData = 0U;
  return 1U;
}
