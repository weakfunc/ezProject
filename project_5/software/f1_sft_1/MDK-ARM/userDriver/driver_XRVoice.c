/*============================================================================
 * driver_XRVoice：XR 语音模块驱动（人形版本 V1）
 * 通过软件 I2C（I2C_BUS_1）与语音模块通信，实现语音命令接收与播报指令发送。
 *
 * 协议格式（读取，6 字节，寄存器 0x00）：
 *   Byte0：标志字节（bit0=1 表示有效命令待读取）
 *   Byte1：0xAA | Byte2：0x55 | Byte3：cmd_type | Byte4：cmd_id | Byte5：0xFB
 *
 * 协议格式（写入，5 字节，寄存器 0x01），两种帧格式：
 *   AA 55 格式（语义 1~10，主动模式）：AA 55 01 XX FB
 *   FF 00 格式（语义11~53，被动播报）：FF 00 XX 00 FF
 *============================================================================*/

#include "driver_XRVoice.h"
#include <string.h>

/*============================================================================
 * 私有宏定义
 *============================================================================*/

/* 接收帧头字节 1（读取侧协议固定头） */
#define XRVOICE_RX_HEADER1         (0xAAU)
/* 接收帧头字节 2 */
#define XRVOICE_RX_HEADER2         (0x55U)
/* 接收帧尾字节 */
#define XRVOICE_RX_TAIL            (0xFBU)
/* 从模块读取的原始数据包长度（字节）：1 字节标志 + 5 字节协议帧 */
#define XRVOICE_RX_RAW_LEN         (6U)
/* 写入模块的协议帧长度（字节），与 h 文件中 XRVOICE_PROTO_FRAME_LEN 一致 */
#define XRVOICE_TX_FRAME_LEN       (5U)
/* 有效标志位掩码：Byte0 的 bit0 */
#define XRVOICE_VALID_FLAG_MASK    (0x01U)
/* 读取寄存器起始地址（文档：从 0x00 读 6 字节） */
#define XRVOICE_REG_ADDR_READ      (0x00U)
/* 写入寄存器起始地址（文档：从 0x01 写 5 字节） */
#define XRVOICE_REG_ADDR_WRITE     (0x01U)
/* 协议命令表中的命令总数 */
#define XRVOICE_CMD_TABLE_SIZE     (53U)
/* 协议命令表对应的起始语义编号（与模块配置一致） */
#define XRVOICE_SEMANTIC_ID_BASE   (1U)

/*============================================================================
 * 私有类型定义
 *============================================================================*/

/* 语音协议命令表条目：存储完整 5 字节发送帧 */
typedef struct {
  uint8_t frame[XRVOICE_TX_FRAME_LEN]; /* 完整发送协议帧 */
} xrVoiceCmdEntry_t;

/*============================================================================
 * 私有变量
 *============================================================================*/

/* XRVoice 公有信息结构体（extern 声明在 .h 中） */
xrVoiceInfo_t xrVoiceInfo;

/* 协议命令表（共 53 条），语义编号 = XRVOICE_SEMANTIC_ID_BASE + 索引（即 1~53）。
 * 语义  1~10：AA 55 01 XX FB（主动模式，模块识别语音后上报，MCU 亦可主动发送）
 * 语义 11~53：FF 00 XX 00 FF（被动播报，MCU 发送触发模块播音）
 */
static const xrVoiceCmdEntry_t xrVoiceCmdTable[XRVOICE_CMD_TABLE_SIZE] = {
  /* ---- 语义 1~10：AA 55 01 XX FB ---- */
  {{0xAAU, 0x55U, 0x01U, 0x00U, 0xFBU}},  /* 语义 1 ：欢迎使用小R科技语音助手 */
  {{0xAAU, 0x55U, 0x01U, 0x01U, 0xFBU}},  /* 语义 2 ：我去休息啦 */
  {{0xAAU, 0x55U, 0x01U, 0x02U, 0xFBU}},  /* 语义 3 ：在的（唤醒词：小二同学） */
  {{0xAAU, 0x55U, 0x01U, 0x03U, 0xFBU}},  /* 语义 4 ：增大音量 */
  {{0xAAU, 0x55U, 0x01U, 0x04U, 0xFBU}},  /* 语义 5 ：减小音量 */
  {{0xAAU, 0x55U, 0x01U, 0x05U, 0xFBU}},  /* 语义 6 ：最大音量 */
  {{0xAAU, 0x55U, 0x01U, 0x06U, 0xFBU}},  /* 语义 7 ：中等音量 */
  {{0xAAU, 0x55U, 0x01U, 0x07U, 0xFBU}},  /* 语义 8 ：最小音量 */
  {{0xAAU, 0x55U, 0x01U, 0x08U, 0xFBU}},  /* 语义 9 ：开启播报 */
  {{0xAAU, 0x55U, 0x01U, 0x09U, 0xFBU}},  /* 语义10：关闭播报 */
  /* ---- 语义 11~27：FF 00 XX 00 FF ---- */
  {{0xFFU, 0x00U, 0x00U, 0x00U, 0xFFU}},  /* 语义11：前方有障碍物 */
  {{0xFFU, 0x00U, 0x01U, 0x00U, 0xFFU}},  /* 语义12：左转 */
  {{0xFFU, 0x00U, 0x02U, 0x00U, 0xFFU}},  /* 语义13：右转 */
  {{0xFFU, 0x00U, 0x03U, 0x00U, 0xFFU}},  /* 语义14：直行 */
  {{0xFFU, 0x00U, 0x04U, 0x00U, 0xFFU}},  /* 语义15：检测到摔倒 */
  {{0xFFU, 0x00U, 0x05U, 0x00U, 0xFFU}},  /* 语义16：现在时间一点整 */
  {{0xFFU, 0x00U, 0x06U, 0x00U, 0xFFU}},  /* 语义17：现在时间两点整 */
  {{0xFFU, 0x00U, 0x07U, 0x00U, 0xFFU}},  /* 语义18：现在时间三点整 */
  {{0xFFU, 0x00U, 0x08U, 0x00U, 0xFFU}},  /* 语义19：现在时间四点整 */
  {{0xFFU, 0x00U, 0x09U, 0x00U, 0xFFU}},  /* 语义20：现在时间五点整 */
  {{0xFFU, 0x00U, 0x0AU, 0x00U, 0xFFU}},  /* 语义21：现在时间六点整 */
  {{0xFFU, 0x00U, 0x0BU, 0x00U, 0xFFU}},  /* 语义22：现在时间七点整 */
  {{0xFFU, 0x00U, 0x0CU, 0x00U, 0xFFU}},  /* 语义23：现在时间八点整 */
  {{0xFFU, 0x00U, 0x0DU, 0x00U, 0xFFU}},  /* 语义24：现在时间九点整 */
  {{0xFFU, 0x00U, 0x0EU, 0x00U, 0xFFU}},  /* 语义25：现在时间十点整 */
  {{0xFFU, 0x00U, 0x0FU, 0x00U, 0xFFU}},  /* 语义26：现在时间十一点整 */
  {{0xFFU, 0x00U, 0x10U, 0x00U, 0xFFU}},  /* 语义27：现在时间十二点整 */
};

/*============================================================================
 * 私有函数
 *============================================================================*/

/* 解析从模块读取的 6 字节原始数据包，提取命令类别和协议编号。
 * rxRaw   ：指向 XRVOICE_RX_RAW_LEN 字节原始缓冲区的指针。
 * cmdType ：输出参数，命令类别字节（AA 55 帧 Byte3）。
 * cmdId   ：输出参数，命令协议编号（AA 55 帧 Byte4）。
 * 返回值  ：1=解析成功（有效命令），0=无效帧或无有效命令标志。
 */
static uint8_t __DRIVER_XRVOICE_ParseRx(
  const uint8_t *rxRaw, uint8_t *cmdType, uint8_t *cmdId)
{
  /* 检查有效标志位（Byte0 的 bit0） */
  if((rxRaw[0] & XRVOICE_VALID_FLAG_MASK) == 0U){
    return 0U;
  }
  /* 校验帧头和帧尾 */
  if((rxRaw[1] != XRVOICE_RX_HEADER1) ||
     (rxRaw[2] != XRVOICE_RX_HEADER2) ||
     (rxRaw[5] != XRVOICE_RX_TAIL)){
    return 0U;
  }
  *cmdType = rxRaw[3];
  *cmdId   = rxRaw[4];
  return 1U;
}

/*============================================================================
 * API 接口
 *============================================================================*/

/* 初始化 XRVoice 驱动：清零公有信息结构体，初始化 I2C 总线。 */
void DRIVER_XRVOICE_Init(void){
  memset(&xrVoiceInfo, 0, sizeof(xrVoiceInfo));
  STDLIB_I2C_Init();
}

/* 轮询一次 I2C，读取 6 字节原始数据并解析协议帧（AA 55 格式）。
 * 若解析到有效语音命令，则更新 xrVoiceInfo.lastCmdType、lastCmdId、cmdReady，返回 1；
 * 否则返回 0。上层读取 cmdReady 后应将其清零。
 */
uint8_t DRIVER_XRVOICE_Poll(void){
  uint8_t rxRaw[XRVOICE_RX_RAW_LEN];
  uint8_t cmdType;
  uint8_t cmdId;

  XRVOICE_DEP_I2C_READ(XRVOICE_REG_ADDR_READ, rxRaw, XRVOICE_RX_RAW_LEN);
  if(__DRIVER_XRVOICE_ParseRx(rxRaw, &cmdType, &cmdId) == 0U){
    return 0U;
  }
  xrVoiceInfo.lastCmdType = cmdType;
  xrVoiceInfo.lastCmdId   = cmdId;
  xrVoiceInfo.cmdReady    = 1U;
  return 1U;
}

/* 向模块发送 5 字节协议帧，通知模块播放对应语音。
 * frame ：指向 XRVOICE_PROTO_FRAME_LEN 字节协议帧的指针（由 FindBySemanticId 获取）。
 */
void DRIVER_XRVOICE_SendCmd(const uint8_t *frame){
  XRVOICE_DEP_I2C_WRITE(XRVOICE_REG_ADDR_WRITE, frame, XRVOICE_TX_FRAME_LEN);
}

/* 根据语义编号（1~53）在协议命令表中查找对应的 5 字节发送帧，拷贝到 frameOut。
 * semanticId ：语义编号（1~53，与模块配置一致）。
 * frameOut   ：输出缓冲区，至少 XRVOICE_PROTO_FRAME_LEN 字节。
 * 返回值     ：1=找到，0=语义编号超出范围。
 */
uint8_t DRIVER_XRVOICE_FindBySemanticId(uint8_t semanticId, uint8_t *frameOut){
  uint8_t idx;
  if((semanticId < XRVOICE_SEMANTIC_ID_BASE) ||
     (semanticId >= (uint8_t)(XRVOICE_SEMANTIC_ID_BASE + XRVOICE_CMD_TABLE_SIZE))){
    return 0U;
  }
  idx = (uint8_t)(semanticId - XRVOICE_SEMANTIC_ID_BASE);
  memcpy(frameOut, xrVoiceCmdTable[idx].frame, XRVOICE_TX_FRAME_LEN);
  return 1U;
}

/* 根据接收到的 cmdType 和 cmdId（AA 55 帧 Byte3/Byte4），在协议表中查找语义编号。
 * 仅对 AA 55 格式（语义 1~10）有效。
 * cmdType    ：AA 55 帧的命令类别字节。
 * cmdId      ：AA 55 帧的命令编号字节。
 * semanticId ：输出参数，找到时写入语义编号（1~10）。
 * 返回值     ：1=找到，0=未找到。
 */
uint8_t DRIVER_XRVOICE_FindByCmdTypeId(
  uint8_t cmdType, uint8_t cmdId, uint8_t *semanticId)
{
  uint8_t i;
  for(i = 0U; i < XRVOICE_CMD_TABLE_SIZE; i++){
    /* 仅匹配 AA 55 格式帧 */
    if((xrVoiceCmdTable[i].frame[0] == XRVOICE_RX_HEADER1) &&
       (xrVoiceCmdTable[i].frame[1] == XRVOICE_RX_HEADER2) &&
       (xrVoiceCmdTable[i].frame[2] == cmdType) &&
       (xrVoiceCmdTable[i].frame[3] == cmdId)){
      *semanticId = (uint8_t)(XRVOICE_SEMANTIC_ID_BASE + i);
      return 1U;
    }
  }
  return 0U;
}
