#ifndef __DRIVER_GY615_H__
#define __DRIVER_GY615_H__

#include "stdlib_usart.h"

/*============================================================================
 * 向下依赖（依赖 stdlib_usart）
 *============================================================================*/

/* GY615 使用的串口端口，默认为串口3，默认波特率9600 */
#define GY615_DEP_UART_PORT                    UART_PORT3
/* 注册自定义字节回调接口 */
#define GY615_DEP_UART_SET_CUSTOM_CB(cb)       STDLIB_USART_SetCustomCb(GY615_DEP_UART_PORT, (cb))
/* 发送原始字节流接口 */
#define GY615_DEP_UART_SEND_RAW(data, len)     STDLIB_USART_SendRaw(GY615_DEP_UART_PORT, (data), (len))

/*============================================================================
 * 向上提供
 *============================================================================*/

/* GY615 默认器件地址（出厂默认 0xA4） */
#define GY615_DEV_ADDR                         (0xA4U)

/**
 * GY615 测温数据结构体
 *
 * 使用示例：
 *   GY615Info_t info;
 *   DRIVER_GY615_Init();           // 初始化，注册串口回调
 *   DRIVER_GY615_Request();        // 发送读温度请求（约10ms后数据就绪）
 *   if(DRIVER_GY615_GetInfo(&info)){
 *       // info.targetTempC  -> 目标温度
 *       // info.ambientTempC -> 环境温度
 *       // info.bodyTempC    -> 额头体温
 *       // info.emissivity   -> 发射率（1~100，对应0.01~1.00）
 *   }
 */
typedef struct {
    float   targetTempC;   /* TO：目标温度（°C） */
    float   ambientTempC;  /* TA：环境温度（°C） */
    float   bodyTempC;     /* BO：额头体温（°C） */
    uint8_t emissivity;    /* E：发射率（1~100，对应 0.01~1.00） */
    uint8_t hasNewData;    /* 数据就绪标志：1=有新数据，0=无 */
} GY615Info_t;

extern GY615Info_t GY615Info;

/* 初始化 GY615 驱动，绑定串口接收回调 , 默认波特率9600*/
void DRIVER_GY615_Init(void);

/* 发送读温度寄存器请求帧，查询目标温度/环境温度/体温/发射率 */
void DRIVER_GY615_Request(void);

/* 获取最新测温数据快照，成功返回1，无新数据返回0 */
uint8_t DRIVER_GY615_GetInfo(GY615Info_t *info);

#endif
