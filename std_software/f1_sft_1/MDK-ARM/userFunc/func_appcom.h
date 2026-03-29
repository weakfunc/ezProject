#ifndef __FUNC_APPCOM_H__
#define __FUNC_APPCOM_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向上提供
 *============================================================================*/

/* STM32→ESP32 CMD 范围（发送方向，共 4 帧）。 */
#define APPCOM_CMD_TX_MIN       (0x09U)
#define APPCOM_CMD_TX_MAX       (0x0CU)
/* ESP32→STM32 CMD 范围（接收方向，共 4 帧）。 */
#define APPCOM_CMD_RX_MIN       (0x13U)
#define APPCOM_CMD_RX_MAX       (0x16U)
/* 发送/接收各 4 帧，数组总长 8。 */
#define APPCOM_TX_COUNT         (4U)
#define APPCOM_RX_COUNT         (4U)
#define APPCOM_FRAME_COUNT      (8U)

/* 单条 APPCOM 数据帧：CMD + 10 字节数据段。 */
typedef struct {
  uint8_t         cmd;     /* 控制字段 */
  standardFrame_t payload; /* 数据段（10 字节，支持具名字段访问） */
} appCmdFrame_t;

/* APPCOM 模块信息结构体。
 * appCmdFrameArr[0~3]：STM32→ESP32（CMD 0x09~0x0C），发送方向；
 * appCmdFrameArr[4~7]：ESP32→STM32（CMD 0x13~0x16），接收方向。
 */
typedef struct {
  appCmdFrame_t appCmdFrameArr[APPCOM_FRAME_COUNT];
} appcomInfo_t;

typedef struct{
  uint8_t null;
  //TX

  //RX
  uint8_t key1;
  uint8_t key2;
  uint8_t key3;
}remoteInfo_t;

extern appcomInfo_t appcomInfo;

extern remoteInfo_t remoteInfo;

/* 初始化 APPCOM 模块，清零数据并设置各帧 CMD 字段。 */
void FUNC_APPCOM_Init(void);

/* 将 appCmdFrameArr[0~3].payload 打包后依次发送给 ESP32。
 * 调用前应先向对应帧的 payload 字段写入待发送数据。
 */
void FUNC_APPCOM_TxUpdate(void);

/* 从 driver_ble 读取最新接收帧，按 CMD 路由到 appCmdFrameArr[4~7].payload，
 * 并将各字段提取到接收目标变量。
 */
void FUNC_APPCOM_RxUpdate(void);

void FUNC_APPCOM_UPDATA(void);

#endif
