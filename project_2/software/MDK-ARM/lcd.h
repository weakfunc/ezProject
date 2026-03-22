#ifndef __lcd_h__
#define __lcd_h__
#include "stdio.h"
#include "gpio.h"

#define LCD_SCLK_Clr() HAL_GPIO_WritePin(GPIOB,GPIO_PIN_10,0)//SCL=SCLK
#define LCD_SCLK_Set() HAL_GPIO_WritePin(GPIOB,GPIO_PIN_10,1)

#define LCD_MOSI_Clr() HAL_GPIO_WritePin(GPIOB,GPIO_PIN_11,0)//SDA=MOSI
#define LCD_MOSI_Set() HAL_GPIO_WritePin(GPIOB,GPIO_PIN_11,1)

#define LCD_RES_Clr()  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_7,0)//RES
#define LCD_RES_Set()  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_7,1)

#define LCD_DC_Clr()   HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,0)//DC
#define LCD_DC_Set()   HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,1)
 		     
#define LCD_CS_Clr()   HAL_GPIO_WritePin(GPIOB,GPIO_PIN_1,0)//CS
#define LCD_CS_Set()   HAL_GPIO_WritePin(GPIOB,GPIO_PIN_1,1)

#define LCD_BLK_Clr()  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_6,0)//BLK
#define LCD_BLK_Set()  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_6,1)

#define USE_HORIZONTAL 2  //设置横屏或者竖屏显示 0或1为竖屏 2或3为横屏


#if USE_HORIZONTAL==0||USE_HORIZONTAL==1
#define LCD_W 80
#define LCD_H 160

#else
#define LCD_W 160
#define LCD_H 80
#endif

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t

//画笔颜色
#define WHITE         	 0xFFFF
#define BLACK         	 0x0000	  
#define BLUE           	 0x001F  
#define BRED             0XF81F
#define GRED 			       0XFFE0
#define GBLUE			       0X07FF
#define RED           	 0xF800
#define MAGENTA       	 0xF81F
#define GREEN         	 0x07E0
#define CYAN          	 0x7FFF
#define YELLOW        	 0xFFE0
#define BROWN 			     0XBC40 //棕色
#define BRRED 			     0XFC07 //棕红色
#define GRAY  			     0X8430 //灰色
#define DARKBLUE      	 0X01CF	//深蓝色
#define LIGHTBLUE      	 0X7D7C	//浅蓝色  
#define GRAYBLUE       	 0X5458 //灰蓝色
#define LIGHTGREEN     	 0X841F //浅绿色
#define LGRAY 			     0XC618 //浅灰色(PANNEL),窗体背景色
#define LGRAYBLUE        0XA651 //浅灰蓝色(中间层颜色)
#define LBBLUE           0X2B12 //浅棕蓝色(选择条目的反色)

void LCD_GPIO_Init(void);//初始化GPIO
void LCD_Writ_Bus(uint8_t dat);//模拟SPI时序
void LCD_WR_DATA8(uint8_t dat);//写入一个字节
void LCD_WR_DATA(uint16_t dat);//写入两个字节
void LCD_WR_REG(uint8_t dat);//写入一个指令
void LCD_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2);//设置坐标函数
void LCD_Init(void);//LCD初始化

void LCD_Fill(u16 xsta,u16 ysta,u16 xend,u16 yend,u16 color);//指定区域填充颜色
void LCD_ShowString(u16 x,u16 y,const u8 *p,u16 fc,u16 bc,u8 sizey,u8 mode);//显示字符串
void LCD_ShowIntNum(u16 x,u16 y,u16 num,u8 len,u16 fc,u16 bc,u8 sizey);
#endif
