/*******************************************************************************
 * @file    driver_mp3.h
 * @brief   MP3-TF-16P 语音模块驱动头文件
 *
 * 【使用说明】
 *  1. 硬件连接：
 *       STM32 USART2_TX  --> MP3模块 RX
 *       STM32 USART2_RX  --> MP3模块 TX（本驱动仅发送，RX可不接）
 *       3.3V/5V          --> MP3模块 VCC
 *       GND              --> MP3模块 GND
 *       喇叭正           --> MP3模块 SPK2
 *       喇叭负           --> MP3模块 SPK1
 *     注意：STM32为5V系统时，建议在TX线串联1K电阻后连接MP3模块RX
 *
 *  2. 串口参数：波特率9600，8位数据位，无校验位，无流控
 *     （需确保USART2已在CubeMX中配置为9600bps）
 *     发送帧格式（8字节，不含校验）：
 *       7E FF 06 CMD 00 Para1 Para2 EF
 *     模块可自动识别不带校验的帧，无需附加校验字节。
 *
 *  3. 上电时序：
 *       模块上电后需等待约1.5~3S完成初始化，初始化期间不可发送命令。
 *       建议在task层调用Init后延时2S再发送其他指令。
 *
 *  4. SD卡文件命名规则（按物理顺序播放）：
 *       直接将mp3/wav文件拷贝到SD卡根目录，模块按存入顺序编号1、2、3...
 *       DRIVER_MP3_PlayTrack(1) 即播放第1首，DRIVER_MP3_PlayTrack(2) 播放第2首，依此类推。
 *
 *  5. 典型调用流程：
 *       DRIVER_MP3_Init();
 *       osDelay(2000);                // 等待模块初始化完成
 *       DRIVER_MP3_PlayTrack(1);      // 播放第1首
 *       osDelay(500);
 *       DRIVER_MP3_Pause();           // 暂停
 *       DRIVER_MP3_Play();            // 继续
 *       DRIVER_MP3_Next();            // 下一首
 *       DRIVER_MP3_Prev();            // 上一首
 ******************************************************************************/

#ifndef __DRIVER_MP3_H__
#define __DRIVER_MP3_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖（driver_mp3 依赖以下 stdlib 接口）
 *   - stdlib_usart: STDLIB_USART_SendRaw
 *============================================================================*/

/* 使用 USART2 与 MP3 模块通信 */
#define MP3_DEP_UART_PORT                  UART_PORT2

/* 原始字节流发送宏 */
#define MP3_DEP_UART_SEND_RAW(data, len)   STDLIB_USART_SendRaw(MP3_DEP_UART_PORT, (data), (len))

/*============================================================================
 * 向上提供
 *============================================================================*/

/* MP3模块信息结构体（模块级状态管理） */
typedef struct {
  uint8_t isInited; /* 是否已完成驱动初始化 */
} mp3Info_t;

/* 对外公开的MP3模块实例 */
extern mp3Info_t mp3Info;

/*----------------------------------------------------------------------------
 * API 函数声明
 *----------------------------------------------------------------------------*/

/* 初始化MP3驱动（仅标记初始化标志，不发送任何串口指令）
 * 注意：调用本函数后需在task层延时约2S，等待MP3模块完成上电初始化后再发送指令 */
void DRIVER_MP3_Init(void);

/* 播放歌曲（恢复已暂停的曲目继续播放，或从停止状态开始播放）
 * 对应串口命令：0x0D */
void DRIVER_MP3_Play(void);

/* 暂停当前播放
 * 对应串口命令：0x0E
 * 再次调用 DRIVER_MP3_Play() 可从暂停处继续 */
void DRIVER_MP3_Pause(void);

/* 按物理存储顺序播放指定曲目
 * @param num  曲目编号，范围 1~2999，对应SD卡中文件的写入顺序
 * 对应串口命令：0x03 */
void DRIVER_MP3_PlayTrack(uint16_t num);

/* 播放下一首（按存储物理顺序切换到下一曲目）
 * 对应串口命令：0x01 */
void DRIVER_MP3_Next(void);

/* 播放上一首（按存储物理顺序切换到上一曲目）
 * 对应串口命令：0x02 */
void DRIVER_MP3_Prev(void);

/* 设置音量
 * @param vol  音量等级，范围 0~30，模块上电默认为30级，超出范围自动截断至30
 * 对应串口命令：0x06 */
void DRIVER_MP3_SetVolume(uint8_t vol);

#endif /* __DRIVER_MP3_H__ */
