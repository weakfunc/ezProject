#ifndef __DRIVER_VERISON_H__
#define __DRIVER_VERISON_H__

#include "stdlib_usart.h"

/* Camera serial protocol:
 * [0]     0x55
 * [1]     0xAA
 * [2]     ctrl
 * [3..12] data, 10 bytes, zero padded
 * [13]    crc8, fixed to 0x00 when CRC is disabled
 * [14]    cnt
 * [15]    0xFF
 */

#define VERISON_DEP_UART_PORT                       UART_PORT3
#define VERISON_DEP_UART_SET_CUSTOM_CB(cb)          STDLIB_USART_SetCustomCb(VERISON_DEP_UART_PORT, (cb))
#define VERISON_DEP_UART_SEND_RAW(buf, len)         STDLIB_USART_SendRaw(VERISON_DEP_UART_PORT, (buf), (len))

#define VERISON_DATA_LEN                            (10U)

typedef struct {
  uint8_t           ctrl;
  uint8_t           data[VERISON_DATA_LEN];
  uint8_t           crc8;
  uint8_t           cnt;
  uint32_t          crcErrTotalCnt;
  uint32_t          rxTotalCnt;
  uint8_t           hasNewData;
  uint8_t           isConnect;
  standardFrame_t   realData;

  /* Valid when the camera sends gimbal target angles as little-endian floats. */
  float             yaw_deg;
  float             pitch_deg;
} verisonInfo_t;

extern verisonInfo_t verisonInfo;

void DRIVER_VERISON_Init(void);
void DRIVER_VERISON_Reset(void);
uint8_t* DRIVER_VERISON_HasNewData(void);

/* 10ms periodic update. Returns 1 when a new frame is consumed. */
uint8_t DRIVER_VERISON_Updata(void);

/* Send one frame to camera: ctrl=0x00, crc=0x00, data is zero padded to 10B. */
uint8_t DRIVER_VERISON_SendData(const uint8_t *data, uint8_t len);

#endif
