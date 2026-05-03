#ifndef __FUNC_GAMBAL_H__
#define __FUNC_GAMBAL_H__

typedef struct {
  float refYaw_deg;    /* 滤波后的 yaw 参考角，单位：度 */
  float refPitch_deg;  /* 滤波后的 pitch 参考角，单位：度 */
  float fbdYaw_deg;    /* 按云台方向换算后的 yaw 反馈角，单位：度 */
  float fbdPitch_deg;  /* 按云台方向换算后的 pitch 反馈角，单位：度 */
} gambalAngleInfo_t;

/* 初始化云台功能模块及其依赖的步进电机驱动。 */
void FUNC_GAMBAL_Init(void);

/* 云台 2ms 周期更新函数。
 * refYaw   ：yaw 参考角，单位：度，内部限幅到 -120~+120。
 * refPitch ：pitch 参考角，单位：度，内部限幅到 -90~+90。
 */
void FUNC_GAMBAL_Updata(float refYaw, float refPitch);

/* 通过 driver_verison 向摄像头发送当前 yaw/pitch 反馈角。 */
void FUNC_GAMBAL_SendCameraAngle(void);

/* 获取 OLED/调试显示使用的云台角度信息。 */
void FUNC_GAMBAL_GetAngleInfo(gambalAngleInfo_t *info);

#endif
