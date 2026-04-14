#include "func_appcom.h"
#include <string.h>

/*============================================================================
 * 公有变量
 *============================================================================*/

/* APPCOM 模块信息（公有，供上层直接访问）。 */
appcomInfo_t appcomInfo;

remoteInfo_t remoteInfo;

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

/* 将 appCmdFrameArr[0~3] 的 payload 依次打包发送（当前BLE模块未启用，保留空实现）。 */
void FUNC_APPCOM_TxUpdate(void){
}

/* 从BLE读取最新接收帧并提取数据（当前BLE模块未启用，保留空实现）。 */
void FUNC_APPCOM_RxUpdate(void){
}


void FUNC_APPCOM_Update(void){
  FUNC_APPCOM_RxUpdate();
  FUNC_APPCOM_TxUpdate();
}
