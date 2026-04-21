#include "func_appcom.h"
#include "driver_ble.h"
#include "stdlib_usart.h"
#include <string.h>
#include "task_user1.h"
#include "task_system.h"


/*============================================================================
 * 私有函数
 *============================================================================*/

/* 32位字节序翻转：将大端序接收到的4字节字段转换为小端序。
 * val : 原始32位值（大端字节顺序）
 * 返回: 字节序翻转后的32位值（小端字节顺序）
 */
static uint32_t __APPCOM_SwapU32(uint32_t val)
{
  return ((val & 0x000000FFU) << 24U) |
         ((val & 0x0000FF00U) <<  8U) |
         ((val & 0x00FF0000U) >>  8U) |
         ((val & 0xFF000000U) >> 24U);
}

/*============================================================================
 * 公有变量
 *============================================================================*/

/* APPCOM 模块信息（公有，供上层直接访问）。 */
appcomInfo_t appcomInfo;

remoteInfo_t remoteInfo;

/* task 层变量引用（手动配置区域使用）。 */
extern user1TaskInfo_t   user1TaskInfo;
extern systemTaskInfo_t  systemTaskInfo;

/*============================================================================
 * API 接口
 *============================================================================*/

/* 初始化 APPCOM 模块，清零帧数组并为每帧设置固定 CMD 字段。 */
void FUNC_APPCOM_Init(void){
  memset(&appcomInfo, 0, sizeof(appcomInfo));
  /* 发送方向：CMD 0x09~0x0C */
  appcomInfo.appCmdFrameArr[0].cmd = 0x09U;
  appcomInfo.appCmdFrameArr[1].cmd = 0x0AU;
  appcomInfo.appCmdFrameArr[2].cmd = 0x0BU;
  appcomInfo.appCmdFrameArr[3].cmd = 0x0CU;
  /* 接收方向：CMD 0x13~0x16 */
  appcomInfo.appCmdFrameArr[4].cmd = 0x13U;
  appcomInfo.appCmdFrameArr[5].cmd = 0x14U;
  appcomInfo.appCmdFrameArr[6].cmd = 0x15U;
  appcomInfo.appCmdFrameArr[7].cmd = 0x16U;
}

/* 将 appCmdFrameArr[0~3] 的 payload 依次打包发送给 ESP32。
 * 手动配置区域：将等号右侧替换为实际来源变量，默认值为 0。
 */
void FUNC_APPCOM_TxUpdate(void){
  uint8_t i;

  /* ================================================================
   * STM32→ESP32 数据装填（手动逐字段赋值，等号右侧为手动配置变量）
   * 格式：appcomInfo.appCmdFrameArr[CMD - 0x09].payload.varXXX = <来源>;
   * ================================================================ */

  /* ---- CMD 0x09 ---- */
  appcomInfo.appCmdFrameArr[0].payload.var_4b_1 = remoteInfo.remoteVar_TX[0].var_uint32;
  appcomInfo.appCmdFrameArr[0].payload.var_4b_2 = remoteInfo.remoteVar_TX[1].var_uint32;
  appcomInfo.appCmdFrameArr[0].payload.var_1b_1 = (uint8_t)remoteInfo.remoteVar_TX[2].var_uint32;
  appcomInfo.appCmdFrameArr[0].payload.var_1b_2 = (uint8_t)remoteInfo.remoteVar_TX[3].var_uint32;

  /* ---- CMD 0x0A ---- */
  appcomInfo.appCmdFrameArr[1].payload.var_4b_1 = remoteInfo.remoteVar_TX[4].var_uint32;
  appcomInfo.appCmdFrameArr[1].payload.var_4b_2 = remoteInfo.remoteVar_TX[5].var_uint32;
  appcomInfo.appCmdFrameArr[1].payload.var_1b_1 = (uint8_t)remoteInfo.remoteVar_TX[6].var_uint32;
  appcomInfo.appCmdFrameArr[1].payload.var_1b_2 = (uint8_t)remoteInfo.remoteVar_TX[7].var_uint32;

  /* ---- CMD 0x0B ---- */
  appcomInfo.appCmdFrameArr[2].payload.var_4b_1 = remoteInfo.remoteVar_TX[8].var_uint32;
  appcomInfo.appCmdFrameArr[2].payload.var_4b_2 = remoteInfo.remoteVar_TX[9].var_uint32;
  appcomInfo.appCmdFrameArr[2].payload.var_1b_1 = (uint8_t)remoteInfo.remoteVar_TX[10].var_uint32;
  appcomInfo.appCmdFrameArr[2].payload.var_1b_2 = (uint8_t)remoteInfo.remoteVar_TX[11].var_uint32;

  /* ---- CMD 0x0C ---- */
  appcomInfo.appCmdFrameArr[3].payload.var_4b_1 = remoteInfo.remoteVar_TX[12].var_uint32;
  appcomInfo.appCmdFrameArr[3].payload.var_4b_2 = remoteInfo.remoteVar_TX[13].var_uint32;
  appcomInfo.appCmdFrameArr[3].payload.var_1b_1 = (uint8_t)remoteInfo.remoteVar_TX[14].var_uint32;
  appcomInfo.appCmdFrameArr[3].payload.var_1b_2 = (uint8_t)remoteInfo.remoteVar_TX[15].var_uint32;

  /* 发送所有 STM32→ESP32 帧 */
  for(i = 0U; i < APPCOM_TX_COUNT; i++){
    DRIVER_BLE_SendFrame(
      appcomInfo.appCmdFrameArr[i].cmd,
      appcomInfo.appCmdFrameArr[i].payload.raw,
      (uint8_t)STM32_DATA_LEN
    );
  }
}

/* 从 driver_ble 读取最新接收帧，按 CMD 路由到 appCmdFrameArr[4~7].payload，
 * 再将各字段提取到对应接收目标变量。无新帧或 CMD 超范围时直接返回。
 */
void FUNC_APPCOM_RxUpdate(void){
  bleFrame_t frame;
  uint8_t    idx;

  /* 先驱动串口解析，将环形缓冲中的字节转为完整帧并置 frame_ready */
  STDLIB_USART_Updata();

  /* 读取最新帧，无新帧则直接返回 */
  if(DRIVER_BLE_GetRxFrame(&frame) == 0U) return;

  /* CMD 超出接收范围则丢弃 */
  if((frame.cmd < APPCOM_CMD_RX_MIN) || (frame.cmd > APPCOM_CMD_RX_MAX)) return;

  /* 按 CMD 路由到对应帧槽位 */
  idx = 4U + (frame.cmd - APPCOM_CMD_RX_MIN);
  memcpy(appcomInfo.appCmdFrameArr[idx].payload.raw, frame.data, STM32_DATA_LEN);

  /* ================================================================
   * ESP32→STM32 数据提取（手动逐字段赋值，等号左侧为手动配置变量）
   * 格式：<目标> = appcomInfo.appCmdFrameArr[4 + (CMD - 0x13)].payload.varXXX;
   * ================================================================ */

  /* ---- CMD 0x13 ---- */
  /* 4字节字段：APP小端序发送，翻转为STM32本地字节序；1字节字段：无需翻转 */
  remoteInfo.remoteVar_RX[0].var_uint32 = __APPCOM_SwapU32(appcomInfo.appCmdFrameArr[4].payload.var_4b_1);
  remoteInfo.remoteVar_RX[1].var_uint32 = __APPCOM_SwapU32(appcomInfo.appCmdFrameArr[4].payload.var_4b_2);
  remoteInfo.systemEnable = appcomInfo.appCmdFrameArr[4].payload.var_1b_1;
  remoteInfo.remoteVar_RX[3].var_uint32 = appcomInfo.appCmdFrameArr[4].payload.var_1b_2;

  /* ---- CMD 0x14 ---- */
  remoteInfo.remoteVar_RX[4].var_uint32 = __APPCOM_SwapU32(appcomInfo.appCmdFrameArr[5].payload.var_4b_1);
  remoteInfo.remoteVar_RX[5].var_uint32 = __APPCOM_SwapU32(appcomInfo.appCmdFrameArr[5].payload.var_4b_2);
  remoteInfo.remoteVar_RX[6].var_uint32 = appcomInfo.appCmdFrameArr[5].payload.var_1b_1;
  remoteInfo.remoteVar_RX[7].var_uint32 = appcomInfo.appCmdFrameArr[5].payload.var_1b_2;

  /* ---- CMD 0x15 ---- */
  remoteInfo.remoteVar_RX[8].var_uint32  = __APPCOM_SwapU32(appcomInfo.appCmdFrameArr[6].payload.var_4b_1);
  remoteInfo.remoteVar_RX[9].var_uint32  = __APPCOM_SwapU32(appcomInfo.appCmdFrameArr[6].payload.var_4b_2);
  remoteInfo.remoteVar_RX[10].var_uint32 = appcomInfo.appCmdFrameArr[6].payload.var_1b_1;
  remoteInfo.remoteVar_RX[11].var_uint32 = appcomInfo.appCmdFrameArr[6].payload.var_1b_2;

  /* ---- CMD 0x16 ---- */
  remoteInfo.remoteVar_RX[12].var_uint32 = __APPCOM_SwapU32(appcomInfo.appCmdFrameArr[7].payload.var_4b_1);
  remoteInfo.remoteVar_RX[13].var_uint32 = __APPCOM_SwapU32(appcomInfo.appCmdFrameArr[7].payload.var_4b_2);
  remoteInfo.remoteVar_RX[14].var_uint32 = appcomInfo.appCmdFrameArr[7].payload.var_1b_1;
  remoteInfo.remoteVar_RX[15].var_uint32 = appcomInfo.appCmdFrameArr[7].payload.var_1b_2;
}


void FUNC_APPCOM_UPDATA(void){
  FUNC_APPCOM_RxUpdate();
  FUNC_APPCOM_TxUpdate();
}
