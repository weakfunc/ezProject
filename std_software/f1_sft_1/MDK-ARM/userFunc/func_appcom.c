#include "func_appcom.h"
#include "driver_ble.h"
#include "stdlib_usart.h"
#include <string.h>
#include "task_user1.h"
#include "task_system.h"


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
  appcomInfo.appCmdFrameArr[0].payload.var_4b_1 = systemTaskInfo.systemTaskCnt;
  appcomInfo.appCmdFrameArr[0].payload.var_4b_2 = systemTaskInfo.systemTaskCnt;
  appcomInfo.appCmdFrameArr[0].payload.var_1b_1 = remoteInfo.key1;
  appcomInfo.appCmdFrameArr[0].payload.var_1b_2 = remoteInfo.key2;

  /* ---- CMD 0x0A ---- */
  appcomInfo.appCmdFrameArr[1].payload.var_4b_1 = systemTaskInfo.systemTaskCnt;
  appcomInfo.appCmdFrameArr[1].payload.var_4b_2 = systemTaskInfo.systemTaskCnt;
  appcomInfo.appCmdFrameArr[1].payload.var_1b_1 = remoteInfo.null;
  appcomInfo.appCmdFrameArr[1].payload.var_1b_2 = remoteInfo.null;

  /* ---- CMD 0x0B ---- */
  appcomInfo.appCmdFrameArr[2].payload.var_4b_1 = systemTaskInfo.systemTaskCnt;
  appcomInfo.appCmdFrameArr[2].payload.var_4b_2 = systemTaskInfo.systemTaskCnt;
  appcomInfo.appCmdFrameArr[2].payload.var_1b_1 = remoteInfo.null;
  appcomInfo.appCmdFrameArr[2].payload.var_1b_2 = remoteInfo.null;

  /* ---- CMD 0x0C ---- */
  appcomInfo.appCmdFrameArr[3].payload.var_4b_1 = systemTaskInfo.systemTaskCnt;
  appcomInfo.appCmdFrameArr[3].payload.var_4b_2 = systemTaskInfo.systemTaskCnt;
  appcomInfo.appCmdFrameArr[3].payload.var_1b_1 = remoteInfo.null;
  appcomInfo.appCmdFrameArr[3].payload.var_1b_2 = remoteInfo.null;

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
  remoteInfo.var[0] = appcomInfo.appCmdFrameArr[4].payload.var_4b_1;
  remoteInfo.var[1] = appcomInfo.appCmdFrameArr[4].payload.var_4b_2;
  remoteInfo.var[2] = appcomInfo.appCmdFrameArr[4].payload.var_1b_1;
  remoteInfo.var[3] = appcomInfo.appCmdFrameArr[4].payload.var_1b_2;

  /* ---- CMD 0x14 ---- */
  remoteInfo.var[4] = appcomInfo.appCmdFrameArr[5].payload.var_4b_1;
  remoteInfo.var[5] = appcomInfo.appCmdFrameArr[5].payload.var_4b_2;
  remoteInfo.var[6] = appcomInfo.appCmdFrameArr[5].payload.var_1b_1;
  remoteInfo.var[7] = appcomInfo.appCmdFrameArr[5].payload.var_1b_2;

  /* ---- CMD 0x15 ---- */
  remoteInfo.var[8] = appcomInfo.appCmdFrameArr[6].payload.var_4b_1;
  remoteInfo.var[9] = appcomInfo.appCmdFrameArr[6].payload.var_4b_2;
  remoteInfo.var[10] = appcomInfo.appCmdFrameArr[6].payload.var_1b_1;
  remoteInfo.var[11] = appcomInfo.appCmdFrameArr[6].payload.var_1b_2;

  /* ---- CMD 0x16 ---- */
  remoteInfo.var[12] = appcomInfo.appCmdFrameArr[7].payload.var_4b_1;
  remoteInfo.var[13] = appcomInfo.appCmdFrameArr[7].payload.var_4b_2;
  remoteInfo.var[14] = appcomInfo.appCmdFrameArr[7].payload.var_1b_1;
  remoteInfo.var[15] = appcomInfo.appCmdFrameArr[7].payload.var_1b_2;
}


void FUNC_APPCOM_UPDATA(void){
  FUNC_APPCOM_RxUpdate();
  FUNC_APPCOM_TxUpdate();
}
