#ifndef __DRIVER_OLED_H__
#define __DRIVER_OLED_H__

#include <stdint.h>
#include "stdlib_i2c.h"

/*============================================================================
 * 向下依赖宏（driver层向stdlib层索要）
 *============================================================================*/

#define OLED_CFG_I2C_ADDR                (0x78U)
#define OLED_DEP_I2C_BUS_ID              I2C_BUS_1
#define OLED_DEP_I2C_CTRL_CMD            (0x00U)
#define OLED_DEP_I2C_CTRL_DATA           (0x40U)

#define OLED_DEP_I2C_INIT()              STDLIB_I2C_Init()
#define OLED_DEP_I2C_WRITE_BYTE(addr, ctrl, data) \
                                         STDLIB_I2C_WriteByte(OLED_DEP_I2C_BUS_ID, (addr), (ctrl), (data))
#define OLED_DEP_I2C_WRITE_DATA(addr, ctrl, data, len) \
                                         STDLIB_I2C_WriteData(OLED_DEP_I2C_BUS_ID, (addr), (ctrl), (data), (len))

/*============================================================================
 * 向上提供宏（driver层向task层提供）
 *============================================================================*/

#define OLED_WIDTH                      (128U)
#define OLED_HEIGHT                     (64U)
#define OLED_PAGE_NUM                   (8U)
#define OLED_MODE_CMD                   (0U)
#define OLED_MODE_DATA                  (1U)

/* OLED模块信息结构体 */
typedef struct {
  uint8_t isOn; /* 显示是否开启：1=开，0=关 */
} oledInfo_t;

extern oledInfo_t oledInfo;

/* OLED 初始化 */
void DRIVER_OLED_Init(void);

/* 刷新显存到屏幕 */
void DRIVER_OLED_Refresh(void);

/* 清屏并刷新 */
void DRIVER_OLED_Clear(void);

/* 开启显示 */
void DRIVER_OLED_DisplayOn(void);

/* 关闭显示 */
void DRIVER_OLED_DisplayOff(void);

/* 颜色反显控制：0正常，1反显 */
void DRIVER_OLED_ColorTurn(uint8_t enable);

/* 显示方向控制：0正常，1旋转180度 */
void DRIVER_OLED_DisplayTurn(uint8_t enable);

/* 绘制单像素点 */
void DRIVER_OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t isSet);

/* 清除单像素点 */
void DRIVER_OLED_ClearPoint(uint8_t x, uint8_t y);

/* 绘制直线 */
void DRIVER_OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t mode);

/* 绘制圆 */
void DRIVER_OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r);

/* 显示单字符（6x8），仅更新显存，需调用 DRIVER_OLED_Refresh() 刷屏 */
void DRIVER_OLED_ShowChar6x8(uint8_t x, uint8_t y, char chr, uint8_t mode);

/* 显示字符串，仅更新显存，需调用 DRIVER_OLED_Refresh() 刷屏 */
void DRIVER_OLED_ShowString(uint8_t x, uint8_t y, const char *str);

/* 显示十进制数字，仅更新显存，需调用 DRIVER_OLED_Refresh() 刷屏 */
void DRIVER_OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len);

/* 显示非负浮点数，仅更新显存，需调用 DRIVER_OLED_Refresh() 刷屏 */
/* decLen为小数位数，返回显示结束后的下一个x坐标 */
uint8_t DRIVER_OLED_ShowFloat(uint8_t x, uint8_t y, float val, uint8_t decLen);

#endif
