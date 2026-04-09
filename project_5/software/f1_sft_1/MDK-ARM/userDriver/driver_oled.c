#include "driver_oled.h"

/*============================================================================
 * 内部配置（仅driver_oled模块内部使用）
 *============================================================================*/

/* OLED 显存缓存，按页存放便于整页连续发送 */
static uint8_t oledGram[OLED_PAGE_NUM][OLED_WIDTH];

/* 6x8 ASCII字模表 */
static const unsigned char asc2_0806[][6] =
{
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},// sp
  {0x00, 0x00, 0x00, 0x2f, 0x00, 0x00},// !
  {0x00, 0x00, 0x07, 0x00, 0x07, 0x00},// "
  {0x00, 0x14, 0x7f, 0x14, 0x7f, 0x14},// #
  {0x00, 0x24, 0x2a, 0x7f, 0x2a, 0x12},// $
  {0x00, 0x62, 0x64, 0x08, 0x13, 0x23},// %
  {0x00, 0x36, 0x49, 0x55, 0x22, 0x50},// &
  {0x00, 0x00, 0x05, 0x03, 0x00, 0x00},// '
  {0x00, 0x00, 0x1c, 0x22, 0x41, 0x00},// (
  {0x00, 0x00, 0x41, 0x22, 0x1c, 0x00},// )
  {0x00, 0x14, 0x08, 0x3E, 0x08, 0x14},// *
  {0x00, 0x08, 0x08, 0x3E, 0x08, 0x08},// +
  {0x00, 0x00, 0x00, 0xA0, 0x60, 0x00},// ,
  {0x00, 0x08, 0x08, 0x08, 0x08, 0x08},// -
  {0x00, 0x00, 0x60, 0x60, 0x00, 0x00},// .
  {0x00, 0x20, 0x10, 0x08, 0x04, 0x02},// /
  {0x00, 0x3E, 0x51, 0x49, 0x45, 0x3E},// 0
  {0x00, 0x00, 0x42, 0x7F, 0x40, 0x00},// 1
  {0x00, 0x42, 0x61, 0x51, 0x49, 0x46},// 2
  {0x00, 0x21, 0x41, 0x45, 0x4B, 0x31},// 3
  {0x00, 0x18, 0x14, 0x12, 0x7F, 0x10},// 4
  {0x00, 0x27, 0x45, 0x45, 0x45, 0x39},// 5
  {0x00, 0x3C, 0x4A, 0x49, 0x49, 0x30},// 6
  {0x00, 0x01, 0x71, 0x09, 0x05, 0x03},// 7
  {0x00, 0x36, 0x49, 0x49, 0x49, 0x36},// 8
  {0x00, 0x06, 0x49, 0x49, 0x29, 0x1E},// 9
  {0x00, 0x00, 0x36, 0x36, 0x00, 0x00},// :
  {0x00, 0x00, 0x56, 0x36, 0x00, 0x00},// ;
  {0x00, 0x08, 0x14, 0x22, 0x41, 0x00},// <
  {0x00, 0x14, 0x14, 0x14, 0x14, 0x14},// =
  {0x00, 0x00, 0x41, 0x22, 0x14, 0x08},// >
  {0x00, 0x02, 0x01, 0x51, 0x09, 0x06},// ?
  {0x00, 0x32, 0x49, 0x59, 0x51, 0x3E},// @
  {0x00, 0x7C, 0x12, 0x11, 0x12, 0x7C},// A
  {0x00, 0x7F, 0x49, 0x49, 0x49, 0x36},// B
  {0x00, 0x3E, 0x41, 0x41, 0x41, 0x22},// C
  {0x00, 0x7F, 0x41, 0x41, 0x22, 0x1C},// D
  {0x00, 0x7F, 0x49, 0x49, 0x49, 0x41},// E
  {0x00, 0x7F, 0x09, 0x09, 0x09, 0x01},// F
  {0x00, 0x3E, 0x41, 0x49, 0x49, 0x7A},// G
  {0x00, 0x7F, 0x08, 0x08, 0x08, 0x7F},// H
  {0x00, 0x00, 0x41, 0x7F, 0x41, 0x00},// I
  {0x00, 0x20, 0x40, 0x41, 0x3F, 0x01},// J
  {0x00, 0x7F, 0x08, 0x14, 0x22, 0x41},// K
  {0x00, 0x7F, 0x40, 0x40, 0x40, 0x40},// L
  {0x00, 0x7F, 0x02, 0x0C, 0x02, 0x7F},// M
  {0x00, 0x7F, 0x04, 0x08, 0x10, 0x7F},// N
  {0x00, 0x3E, 0x41, 0x41, 0x41, 0x3E},// O
  {0x00, 0x7F, 0x09, 0x09, 0x09, 0x06},// P
  {0x00, 0x3E, 0x41, 0x51, 0x21, 0x5E},// Q
  {0x00, 0x7F, 0x09, 0x19, 0x29, 0x46},// R
  {0x00, 0x46, 0x49, 0x49, 0x49, 0x31},// S
  {0x00, 0x01, 0x01, 0x7F, 0x01, 0x01},// T
  {0x00, 0x3F, 0x40, 0x40, 0x40, 0x3F},// U
  {0x00, 0x1F, 0x20, 0x40, 0x20, 0x1F},// V
  {0x00, 0x3F, 0x40, 0x38, 0x40, 0x3F},// W
  {0x00, 0x63, 0x14, 0x08, 0x14, 0x63},// X
  {0x00, 0x07, 0x08, 0x70, 0x08, 0x07},// Y
  {0x00, 0x61, 0x51, 0x49, 0x45, 0x43},// Z
  {0x00, 0x00, 0x7F, 0x41, 0x41, 0x00},// [
  {0x00, 0x55, 0x2A, 0x55, 0x2A, 0x55},// 55
  {0x00, 0x00, 0x41, 0x41, 0x7F, 0x00},// ]
  {0x00, 0x04, 0x02, 0x01, 0x02, 0x04},// ^
  {0x00, 0x40, 0x40, 0x40, 0x40, 0x40},// _
  {0x00, 0x00, 0x01, 0x02, 0x04, 0x00},// '
  {0x00, 0x20, 0x54, 0x54, 0x54, 0x78},// a
  {0x00, 0x7F, 0x48, 0x44, 0x44, 0x38},// b
  {0x00, 0x38, 0x44, 0x44, 0x44, 0x20},// c
  {0x00, 0x38, 0x44, 0x44, 0x48, 0x7F},// d
  {0x00, 0x38, 0x54, 0x54, 0x54, 0x18},// e
  {0x00, 0x08, 0x7E, 0x09, 0x01, 0x02},// f
  {0x00, 0x18, 0xA4, 0xA4, 0xA4, 0x7C},// g
  {0x00, 0x7F, 0x08, 0x04, 0x04, 0x78},// h
  {0x00, 0x00, 0x44, 0x7D, 0x40, 0x00},// i
  {0x00, 0x40, 0x80, 0x84, 0x7D, 0x00},// j
  {0x00, 0x7F, 0x10, 0x28, 0x44, 0x00},// k
  {0x00, 0x00, 0x41, 0x7F, 0x40, 0x00},// l
  {0x00, 0x7C, 0x04, 0x18, 0x04, 0x78},// m
  {0x00, 0x7C, 0x08, 0x04, 0x04, 0x78},// n
  {0x00, 0x38, 0x44, 0x44, 0x44, 0x38},// o
  {0x00, 0xFC, 0x24, 0x24, 0x24, 0x18},// p
  {0x00, 0x18, 0x24, 0x24, 0x18, 0xFC},// q
  {0x00, 0x7C, 0x08, 0x04, 0x04, 0x08},// r
  {0x00, 0x48, 0x54, 0x54, 0x54, 0x20},// s
  {0x00, 0x04, 0x3F, 0x44, 0x40, 0x20},// t
  {0x00, 0x3C, 0x40, 0x40, 0x20, 0x7C},// u
  {0x00, 0x1C, 0x20, 0x40, 0x20, 0x1C},// v
  {0x00, 0x3C, 0x40, 0x30, 0x40, 0x3C},// w
  {0x00, 0x44, 0x28, 0x10, 0x28, 0x44},// x
  {0x00, 0x1C, 0xA0, 0xA0, 0xA0, 0x7C},// y
  {0x00, 0x44, 0x64, 0x54, 0x4C, 0x44},// z
  {0x14, 0x14, 0x14, 0x14, 0x14, 0x14},// horiz lines
};

/* OLED模块数据 */
oledInfo_t oledInfo;

/* 通过 I2C 写命令/数据字节 */
static void __DRIVER_OLED_WriteByte(uint8_t dat, uint8_t mode){
    if(mode == OLED_MODE_DATA){
        OLED_DEP_I2C_WRITE_BYTE(OLED_CFG_I2C_ADDR, OLED_DEP_I2C_CTRL_DATA, dat);
    } else {
        OLED_DEP_I2C_WRITE_BYTE(OLED_CFG_I2C_ADDR, OLED_DEP_I2C_CTRL_CMD, dat);
    }
}

/* 计算 base 的 exp 次幂 */
static uint32_t __DRIVER_OLED_Pow(uint8_t base, uint8_t exp){
    uint32_t result = 1U;
    while(exp--){
        result *= base;
    }
    return result;
}

/*============================================================================
 * API接口
 *============================================================================*/

/* 设置反显模式 */
void DRIVER_OLED_ColorTurn(uint8_t enable){
    if(enable == 0U){
        __DRIVER_OLED_WriteByte(0xA6U, OLED_MODE_CMD);
    } else {
        __DRIVER_OLED_WriteByte(0xA7U, OLED_MODE_CMD);
    }
}

/* 设置屏幕显示方向 */
void DRIVER_OLED_DisplayTurn(uint8_t enable){
    if(enable == 0U){
        __DRIVER_OLED_WriteByte(0xC8U, OLED_MODE_CMD);
        __DRIVER_OLED_WriteByte(0xA1U, OLED_MODE_CMD);
    } else {
        __DRIVER_OLED_WriteByte(0xC0U, OLED_MODE_CMD);
        __DRIVER_OLED_WriteByte(0xA0U, OLED_MODE_CMD);
    }
}

/* 开启 OLED 显示 */
void DRIVER_OLED_DisplayOn(void){
    __DRIVER_OLED_WriteByte(0x8DU, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x14U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xAFU, OLED_MODE_CMD);
    oledInfo.isOn = 1U;
}

/* 关闭 OLED 显示 */
void DRIVER_OLED_DisplayOff(void){
    __DRIVER_OLED_WriteByte(0x8DU, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x10U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xAEU, OLED_MODE_CMD);
    oledInfo.isOn = 0U;
}

/* 刷新显存到 OLED */
void DRIVER_OLED_Refresh(void){
    for(uint8_t page = 0U; page < OLED_PAGE_NUM; page++){
        __DRIVER_OLED_WriteByte((uint8_t)(0xB0U + page), OLED_MODE_CMD);
        __DRIVER_OLED_WriteByte(0x00U, OLED_MODE_CMD);
        __DRIVER_OLED_WriteByte(0x10U, OLED_MODE_CMD);
        OLED_DEP_I2C_WRITE_DATA(OLED_CFG_I2C_ADDR,
                                OLED_DEP_I2C_CTRL_DATA,
                                &oledGram[page][0],
                                OLED_WIDTH);
    }
}

/* 清空显存并刷新 */
void DRIVER_OLED_Clear(void){
    for(uint8_t page = 0U; page < OLED_PAGE_NUM; page++){
        for(uint8_t x = 0U; x < OLED_WIDTH; x++){
            oledGram[page][x] = 0x00U;
        }
    }
}

/* 绘制像素点 */
void DRIVER_OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t isSet){
    uint8_t page;
    uint8_t bitPos;
    uint8_t bitMask;

    page = (uint8_t)(y / 8U);
    bitPos = (uint8_t)(y % 8U);
    bitMask = (uint8_t)(1U << bitPos);

    if(isSet != 0U){
        oledGram[page][x] |= bitMask;
    } else {
        oledGram[page][x] &= (uint8_t)(~bitMask);
    }
}

/* 清除像素点 */
void DRIVER_OLED_ClearPoint(uint8_t x, uint8_t y){
    DRIVER_OLED_DrawPoint(x, y, 0U);
}

/* Bresenham 直线绘制 */
void DRIVER_OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t mode){
    int32_t xErr = 0;
    int32_t yErr = 0;
    int32_t deltaX = (int32_t)x2 - (int32_t)x1;
    int32_t deltaY = (int32_t)y2 - (int32_t)y1;
    int32_t distance;
    int32_t incX;
    int32_t incY;
    int32_t row = x1;
    int32_t col = y1;

    if(deltaX > 0){
        incX = 1;
    } else if(deltaX == 0){
        incX = 0;
    } else {
        incX = -1;
        deltaX = -deltaX;
    }

    if(deltaY > 0){
        incY = 1;
    } else if(deltaY == 0){
        incY = 0;
    } else {
        incY = -1;
        deltaY = -deltaY;
    }

    distance = (deltaX > deltaY) ? deltaX : deltaY;

    for(int32_t t = 0; t <= distance; t++){
        DRIVER_OLED_DrawPoint((uint8_t)row, (uint8_t)col, mode);
        xErr += deltaX;
        yErr += deltaY;

        if(xErr > distance){
            xErr -= distance;
            row += incX;
        }

        if(yErr > distance){
            yErr -= distance;
            col += incY;
        }
    }
}

/* 圆形绘制 */
void DRIVER_OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r){
    int32_t a = 0;
    int32_t b = r;

    while((2 * b * b) >= (r * r)){
        DRIVER_OLED_DrawPoint((uint8_t)(x + a), (uint8_t)(y - b), 1U);
        DRIVER_OLED_DrawPoint((uint8_t)(x - a), (uint8_t)(y - b), 1U);
        DRIVER_OLED_DrawPoint((uint8_t)(x - a), (uint8_t)(y + b), 1U);
        DRIVER_OLED_DrawPoint((uint8_t)(x + a), (uint8_t)(y + b), 1U);
        DRIVER_OLED_DrawPoint((uint8_t)(x + b), (uint8_t)(y + a), 1U);
        DRIVER_OLED_DrawPoint((uint8_t)(x + b), (uint8_t)(y - a), 1U);
        DRIVER_OLED_DrawPoint((uint8_t)(x - b), (uint8_t)(y - a), 1U);
        DRIVER_OLED_DrawPoint((uint8_t)(x - b), (uint8_t)(y + a), 1U);

        a++;
        if(((a * a) + (b * b) - (r * r)) > 0){
            b--;
            a--;
        }
    }
}

/* 显示单个 6x8 字符 */
void DRIVER_OLED_ShowChar6x8(uint8_t x, uint8_t y, char chr, uint8_t mode){
    uint8_t chrOffset;

    if(((uint8_t)chr < (uint8_t)' ') || ((uint8_t)chr > (uint8_t)'~')){
        chr = ' ';
    }

    chrOffset = (uint8_t)chr - (uint8_t)' ';
    for(uint8_t i = 0U; i < 6U; i++){
        uint8_t temp = asc2_0806[chrOffset][i];
        for(uint8_t bit = 0U; bit < 8U; bit++){
            if((temp & 0x01U) != 0U){
                DRIVER_OLED_DrawPoint((uint8_t)(x + i), (uint8_t)(y + bit), mode);
            } else {
                DRIVER_OLED_DrawPoint((uint8_t)(x + i), (uint8_t)(y + bit), (uint8_t)(!mode));
            }
            temp >>= 1U;
        }
    }

}

/* 显示字符串 */
void DRIVER_OLED_ShowString(uint8_t x, uint8_t y, const char *str){
    uint8_t xPos = x;
    const char *p = str;

    while(*p != '\0'){
        DRIVER_OLED_ShowChar6x8(xPos, y, *p, 1U);
        xPos = (uint8_t)(xPos + 6U);
        p++;
    }
}

/* 显示十进制数字 */
void DRIVER_OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len){
    for(uint8_t i = 0U; i < len; i++){
        uint8_t digit = (uint8_t)((num / __DRIVER_OLED_Pow(10U, (uint8_t)(len - i - 1U))) % 10U);
        DRIVER_OLED_ShowChar6x8((uint8_t)(x + i * 6U), y, (char)('0' + digit), 1U);
    }
}

/* 显示非负浮点数，decLen为小数位数，返回显示结束后的下一个x坐标。
 * 小数部分四舍五入到decLen位；decLen为0时只显示整数部分。 */
uint8_t DRIVER_OLED_ShowFloat(uint8_t x, uint8_t y, float val, uint8_t decLen){
    uint32_t intPart;
    uint32_t decPart;
    uint32_t decMul;
    uint32_t tmp;
    uint8_t  intLen;
    uint8_t  xPos = x;

    /* 处理负数：显示负号后对绝对值操作 */
    if(val < 0.0f){
        DRIVER_OLED_ShowChar6x8(xPos, y, '-', 1U);
        xPos = (uint8_t)(xPos + 6U);
        val = -val;
    }

    intPart = (uint32_t)val;

    /* 计算小数部分，四舍五入到decLen位 */
    decMul  = __DRIVER_OLED_Pow(10U, decLen);
    decPart = (uint32_t)((val - (float)intPart) * (float)decMul + 0.5f);
    if(decPart >= decMul){ intPart++; decPart = 0U; }  /* 小数进位处理 */

    /* 计算整数部分位数（至少1位） */
    tmp    = intPart;
    intLen = 1U;
    while(tmp >= 10U){ tmp /= 10U; intLen++; }

    /* 显示整数部分 */
    DRIVER_OLED_ShowNum(xPos, y, intPart, intLen);
    xPos = (uint8_t)(xPos + (uint8_t)(intLen * 6U));

    /* 显示小数点和小数部分（保留前导零） */
    if(decLen > 0U){
        DRIVER_OLED_ShowChar6x8(xPos, y, '.', 1U);
        xPos = (uint8_t)(xPos + 6U);
        DRIVER_OLED_ShowNum(xPos, y, decPart, decLen);
        xPos = (uint8_t)(xPos + (uint8_t)(decLen * 6U));
    }

    return xPos;
}

/* OLED 初始化 */
void DRIVER_OLED_Init(void){
    OLED_DEP_I2C_INIT();

    __DRIVER_OLED_WriteByte(0xAEU, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x00U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x10U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x40U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x81U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xCFU, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xA1U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xC8U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xA6U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xA8U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x3FU, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xD3U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x00U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xD5U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x80U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xD9U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xF1U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xDAU, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x12U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xDBU, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x30U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x20U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x02U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x8DU, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x14U, OLED_MODE_CMD);

    DRIVER_OLED_Clear();
    __DRIVER_OLED_WriteByte(0xAFU, OLED_MODE_CMD);

    DRIVER_OLED_DisplayOn();
	DRIVER_OLED_ColorTurn(0);
    DRIVER_OLED_DisplayTurn(0);   //屏幕反转
}
