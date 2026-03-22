#include "driver_oled.h"

/*============================================================================
 * 内部配置（仅driver_oled模块内部使用）
 *============================================================================*/

/* OLED 显存缓存，按页存放便于整页连续发送 */
static uint8_t oledGram[OLED_PAGE_NUM][OLED_WIDTH];

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
}

/* 关闭 OLED 显示 */
void DRIVER_OLED_DisplayOff(void){
    __DRIVER_OLED_WriteByte(0x8DU, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0x10U, OLED_MODE_CMD);
    __DRIVER_OLED_WriteByte(0xAEU, OLED_MODE_CMD);
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
    DRIVER_OLED_Refresh();
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

    DRIVER_OLED_Refresh();
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
