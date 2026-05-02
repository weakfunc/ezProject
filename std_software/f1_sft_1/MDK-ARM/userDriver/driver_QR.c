/*============================================================================
 * 二维码扫描模块驱动
 * 通过USART2接收原始字节，无协议封装，STM32收到的所有字节均为数据字段
 * 默认每帧QR_DATA_LEN字节（在driver_qr.h中修改）
 * 每帧接收完毕后进入QR_COOLDOWN_MS冷却期，冷却期内忽略串口数据
 *============================================================================*/

#include "driver_qr.h"

/*============================================================================
 * 内部配置（仅driver_qr模块内部使用）
 *============================================================================*/

/* 接收一帧后的冷却时长（毫秒，可修改） */
#define QR_COOLDOWN_MS  2000U

/* 数据段接收计数 */
static uint8_t  qrDataIdx = 0U;
/* 数据段临时缓冲区 */
static uint8_t  qrDataTmp[QR_DATA_LEN];
/* 冷却期标志：1=冷却中，0=可接收 */
static uint8_t  qrInCooldown = 0U;
/* 冷却开始时刻（HAL_GetTick毫秒值） */
static uint32_t qrCooldownStartMs = 0U;

/* 二维码模块信息结构体（对外暴露） */
qrInfo_t qrInfo;

/* USART2字节回调：冷却期内丢弃字节，满QR_DATA_LEN字节后更新qrInfo并进入冷却 */
static void __DRIVER_QR_UartByteCallback(uint8_t port, uint8_t byte){
  if(port != QR_DEP_UART_PORT) return;

  /* 冷却期检查：未到期则丢弃当前字节 */
  if(qrInCooldown){
    if((HAL_GetTick() - qrCooldownStartMs) >= QR_COOLDOWN_MS){
      qrInCooldown = 0U;
      qrDataIdx    = 0U;
    } else {
      return;
    }
  }

  qrDataTmp[qrDataIdx++] = byte;
  if(qrDataIdx >= QR_DATA_LEN){
    qrDataIdx = 0U;
    qrInfo.data1      = qrDataTmp[0];
    qrInfo.hasNewData = 1U;
    qrInfo.rxTotalCnt++;
    /* 接收完一帧，启动冷却 */
    qrCooldownStartMs = HAL_GetTick();
    qrInCooldown      = 1U;
  }
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化二维码扫描驱动，注册USART2字节回调 */
void DRIVER_QR_Init(void){
  qrDataIdx         = 0U;
  qrInCooldown      = 0U;
  qrCooldownStartMs = 0U;
  qrInfo.data1      = 0U;
  qrInfo.hasNewData = 0U;
  qrInfo.rxTotalCnt = 0U;
  QR_DEP_UART_SET_CUSTOM_CB(__DRIVER_QR_UartByteCallback);
}
