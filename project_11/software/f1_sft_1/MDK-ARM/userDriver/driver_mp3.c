/*============================================================================
 * README
 *
 *============================================================================*/

/*******************************************************************************
 * @file    driver_mp3.c
 * @brief   MP3-TF-16P 语音模块驱动实现
 *
 * 串口协议帧格式（共8字节，不含校验）：
 *   [0x7E][0xFF][0x06][CMD][0x00][Para1][Para2][0xEF]
 *
 * 模块会自动识别不带校验的帧格式，无需附加校验字节。
 ******************************************************************************/

#include "driver_mp3.h"
#include "stdlib_dwt.h"

/*============================================================================
 * 内部配置（仅 driver_mp3 模块内部使用）
 *============================================================================*/

/* 帧定界符与版本号 */
#define MP3_FRAME_START    0x7EU  /* 帧起始字节 */
#define MP3_FRAME_VER      0xFFU  /* 版本号，固定0xFF */
#define MP3_FRAME_LEN      0x06U  /* 数据长度（不含起始、结束和校验） */
#define MP3_FRAME_END      0xEFU  /* 帧结束字节 */
#define MP3_FRAME_NO_ACK   0x00U  /* 不需要应答 */

/* 命令字 */
#define MP3_CMD_NEXT       0x01U  /* 下一曲 */
#define MP3_CMD_PREV       0x02U  /* 上一曲 */
#define MP3_CMD_PLAY_TRACK 0x03U  /* 指定曲目播放（按物理存储顺序） */
#define MP3_CMD_SET_VOL    0x06U  /* 指定音量（0~30） */
#define MP3_CMD_PLAY       0x0DU  /* 播放 */
#define MP3_CMD_PAUSE      0x0EU  /* 暂停 */

/* 帧总字节数（不含校验，共8字节） */
#define MP3_FRAME_TOTAL    8U

/* MP3模块实例 */
mp3Info_t mp3Info;

/*============================================================================
 * 内部函数
 *============================================================================*/

/**
 * @brief  构建并发送一帧MP3串口命令
 * @param  cmd    命令字节
 * @param  para1  参数高字节
 * @param  para2  参数低字节
 */
static void mp3SendCmd(uint8_t cmd, uint8_t para1, uint8_t para2)
{
  /* static：DMA异步搬运期间函数已返回，栈变量会被覆盖导致数据错误，
   * 改为静态变量可保证DMA读取期间内存有效 */
  static uint8_t frame[MP3_FRAME_TOTAL];

  /* 填充帧数据（不含校验字节，模块自动识别） */
  frame[0] = MP3_FRAME_START;
  frame[1] = MP3_FRAME_VER;
  frame[2] = MP3_FRAME_LEN;
  frame[3] = cmd;
  frame[4] = MP3_FRAME_NO_ACK;
  frame[5] = para1;
  frame[6] = para2;
  frame[7] = MP3_FRAME_END;

  MP3_DEP_UART_SEND_RAW(frame, MP3_FRAME_TOTAL);
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 初始化MP3驱动 */
void DRIVER_MP3_Init(void)
{
  mp3Info.isInited = 1U;
  STDLIB_DWT_DelayMs(2000);
}

/* 播放歌曲 */
void DRIVER_MP3_Play(void)
{
  mp3SendCmd(MP3_CMD_PLAY, 0x00U, 0x00U);
}

/* 暂停播放 */
void DRIVER_MP3_Pause(void)
{
  mp3SendCmd(MP3_CMD_PAUSE, 0x00U, 0x00U);
}

/* 播放指定曲目 */
void DRIVER_MP3_PlayTrack(uint16_t num)
{
  mp3SendCmd(MP3_CMD_PLAY_TRACK,
             (uint8_t)(num >> 8U),
             (uint8_t)(num & 0xFFU));
}

/* 播放下一首 */
void DRIVER_MP3_Next(void)
{
  mp3SendCmd(MP3_CMD_NEXT, 0x00U, 0x00U);
}

/* 播放上一首 */
void DRIVER_MP3_Prev(void)
{
  mp3SendCmd(MP3_CMD_PREV, 0x00U, 0x00U);
}

/* 设置音量 */
void DRIVER_MP3_SetVolume(uint8_t vol)
{
  if(vol > 30U){ vol = 30U; }
  mp3SendCmd(MP3_CMD_SET_VOL, 0x00U, vol);
}
