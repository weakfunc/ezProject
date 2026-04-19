#ifndef __FUNC_FUNC_H__
#define __FUNC_FUNC_H__

#include <stdint.h>

/*============================================================================
 * 向上提供宏（func层向task层提供）
 *============================================================================*/

/* 目标速度占空比宏定义 */
#define SPEED_5MS    (100U)   /* 5 m/s 对应PWM占空比（%） */
#define SPEED_3MS    (60)    /* 3 m/s 对应PWM占空比（%） */
#define SPEED_1MS    (30)    /* 1 m/s 对应PWM占空比（%） */

/* 发射器功能模块信息结构体 */
typedef struct {
    float   targetSpeed;   /* 目标速度（m/s） */
    float   actualSpeed;   /* 实际速度（m/s），由红外测速模块写入 */
    uint8_t targetDuty;    /* 当前目标占空比（%） */
    uint8_t measError;     /* 测量错误标志：1=测速结果超出目标速度±10%容差，0=正常 */
} funcInfo_t;

extern funcInfo_t funcInfo;

/* 功能模块初始化，设置默认目标速度并启动PWM */
void FUNC_FUNC_Init(void);

/* 按键扫描更新，检测按键上升沿并更新目标速度及PWM占空比，需周期调用 */
void FUNC_FUNC_KeyUpdate(void);

/* 刷新OLED显存内容（目标速度+实际速度），需配合DRIVER_OLED_Refresh()使用 */
void FUNC_FUNC_DrawUI(void);

/* 红外对管测速更新，检测对管触发上升沿并计算实际速度，需周期调用 */
void FUNC_FUNC_SpeedMeasUpdate(void);

#endif
