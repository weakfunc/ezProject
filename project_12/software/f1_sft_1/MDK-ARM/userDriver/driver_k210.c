#include "driver_k210.h"

/*============================================================================
 * 内部配置（仅driver_k210模块内部使用）
 *============================================================================*/
#define K210_FRAME_HEAD_1                (0x55U)
#define K210_FRAME_HEAD_2                (0xAAU)
#define K210_FRAME_TAIL                  (0xBBU)

/* K210固定帧逐字节解析状态 */
typedef enum {
  K210_PARSE_WAIT_HEAD_1 = 0U,
  K210_PARSE_WAIT_HEAD_2,
  K210_PARSE_WAIT_DATA,
  K210_PARSE_WAIT_TAIL,
} k210ParseState_e;

/* K210解析运行时上下文 */
typedef struct {
  k210ParseState_e parseState;  /* 当前解析状态 */
  uint8_t          dataByte;    /* 暂存数据段1字节数据 */
} k210ParseInfo_t;

/* K210解析状态缓存 */
k210ParseInfo_t k210ParseInfo;
/* K210最近一次解析得到的数据字节 */
volatile uint8_t k210RxData = 0U;
/* 新帧到达标志：每成功解析一帧置1，由上层读取后清零 */
volatile uint8_t k210RxFlag = 0U;

/* 重置K210固定帧解析状态 */
static void __DRIVER_K210_ResetParse(void){
  k210ParseInfo.parseState = K210_PARSE_WAIT_HEAD_1;
  k210ParseInfo.dataByte = 0U;
}

/* 按固定4字节协议逐字节解析K210串口数据 */
static void __DRIVER_K210_ParseByte(uint8_t byte){
  switch(k210ParseInfo.parseState){
  case K210_PARSE_WAIT_HEAD_1:
    if(byte == K210_FRAME_HEAD_1){
      k210ParseInfo.parseState = K210_PARSE_WAIT_HEAD_2;
    }
    break;

  case K210_PARSE_WAIT_HEAD_2:
    if(byte == K210_FRAME_HEAD_2){
      k210ParseInfo.parseState = K210_PARSE_WAIT_DATA;
    } else if(byte == K210_FRAME_HEAD_1){
      k210ParseInfo.parseState = K210_PARSE_WAIT_HEAD_2;
    } else {
      __DRIVER_K210_ResetParse();
    }
    break;

  case K210_PARSE_WAIT_DATA:
    k210ParseInfo.dataByte = byte;
    k210ParseInfo.parseState = K210_PARSE_WAIT_TAIL;
    break;

  case K210_PARSE_WAIT_TAIL:
    if(byte == K210_FRAME_TAIL){
      k210RxData = k210ParseInfo.dataByte;
      k210RxFlag = 1U;
      __DRIVER_K210_ResetParse();
    } else if(byte == K210_FRAME_HEAD_1){
      k210ParseInfo.parseState = K210_PARSE_WAIT_HEAD_2;
    } else {
      __DRIVER_K210_ResetParse();
    }
    break;

  default:
    __DRIVER_K210_ResetParse();
    break;
  }
}

/* USART2自定义协议逐字节回调 */
static void __DRIVER_K210_RxParseCb(uint8_t port, uint8_t byte){
  (void)port;
  __DRIVER_K210_ParseByte(byte);
}

/* 初始化K210驱动，注册USART2字节解析回调 */
void DRIVER_K210_Init(void){
  DRIVER_K210_Reset();
  K210_DEP_UART_SET_CUSTOM_CB(__DRIVER_K210_RxParseCb);
}

/* 重置K210驱动解析状态和数据缓存 */
void DRIVER_K210_Reset(void){
  __DRIVER_K210_ResetParse();
  k210RxData = 0U;
  k210RxFlag = 0U;
}
