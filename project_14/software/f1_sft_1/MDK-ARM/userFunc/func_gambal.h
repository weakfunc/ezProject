#ifndef __FUNC_GAMBAL_H__
#define __FUNC_GAMBAL_H__

/* Gimbal angle control, called by userTask every 2ms.
 * refYaw   : yaw reference angle, deg, limited to -120~+120.
 * refPitch : pitch reference angle, deg, limited to -90~+90.
 */
void FUNC_GAMBAL_Init(void);
void FUNC_GAMBAL_Updata(float refYaw, float refPitch);
void FUNC_GAMBAL_SendCameraAngle(void);

#endif
