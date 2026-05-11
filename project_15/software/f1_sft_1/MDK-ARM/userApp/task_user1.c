#include "task_user1.h"
#include "func_appcom.h"
#include "driver_board.h"
#include "driver_oled.h"
#include "driver_ble.h"
#include "func_gambal.h"

user1TaskInfo_t user1TaskInfo;

static void __USER1_OledShowAngle(uint8_t y, const char *name, float value) {
	DRIVER_OLED_ShowString(0, y, name);
	(void)DRIVER_OLED_ShowFloat(42, y, value, 1U);
}

static void __USER1_OledUpdata(void) {
	gambalAngleInfo_t angleInfo;

	FUNC_GAMBAL_GetAngleInfo(&angleInfo);

	DRIVER_OLED_Clear();

	/* 现有 OLED 字库仅支持 ASCII，这里使用英文缩写显示连接和角度。 */


	__USER1_OledShowAngle(12, "REF_P:", angleInfo.refPitch_deg);
	__USER1_OledShowAngle(24, "REF_Y:", angleInfo.refYaw_deg);
	__USER1_OledShowAngle(36, "FBD_P:", angleInfo.fbdPitch_deg);
	__USER1_OledShowAngle(48, "FBD_Y:", angleInfo.fbdYaw_deg);

	DRIVER_OLED_Refresh();
}

void user1TaskInit(){
	DRIVER_BLE_Init();
	/* 等待OLED VCC上电稳定（SSD1306要求VCC稳定后≥100ms才可接受I2C命令） */
	osDelay(100);
	
	DRIVER_OLED_Init();
	FUNC_GAMBAL_Init();
}

/* 三种形状各 24 个采样点，按 key[3] 切换（正方形→三角形→圆形→循环）。
 * 每点停留 4ms（一个双轴控制周期），一圈周期 = 24×4ms = 96ms ≈ 10.4Hz。 */
#define SHAPE_COUNT   3U
#define SHAPE_PT_CNT  24U

static const float sShapeX[SHAPE_COUNT][SHAPE_PT_CNT] = {
  { /* 0: 正方形，边长50，中心原点，每边6点，步长≈8.33 */
    /* 边1 X=-25，Y从-25↑ */ -25.00f,-25.00f,-25.00f,-25.00f,-25.00f,-25.00f,
    /* 边2 Y=25，X从-25→25 */ -25.00f,-16.67f, -8.33f,  0.00f,  8.33f, 16.67f,
    /* 边3 X=25，Y从25↓   */  25.00f, 25.00f, 25.00f, 25.00f, 25.00f, 25.00f,
    /* 边4 Y=-25，X从25→-25*/ 25.00f, 16.67f,  8.33f,  0.00f, -8.33f,-16.67f
  },
  { /* 1: 等边三角形，边长50，中心原点，V1(0,28.87)→V2(-25,-14.43)→V3(25,-14.43)，每边8点 */
    /* 边1 V1→V2 */   0.00f, -3.13f, -6.25f, -9.38f,-12.50f,-15.63f,-18.75f,-21.88f,
    /* 边2 V2→V3 */ -25.00f,-18.75f,-12.50f, -6.25f,  0.00f,  6.25f, 12.50f, 18.75f,
    /* 边3 V3→V1 */  25.00f, 21.88f, 18.75f, 15.63f, 12.50f,  9.38f,  6.25f,  3.13f
  },
  { /* 2: 圆形，半径25，24等分，步长15° */
     25.00f, 24.15f, 21.65f, 17.68f, 12.50f,  6.47f,
      0.00f, -6.47f,-12.50f,-17.68f,-21.65f,-24.15f,
    -25.00f,-24.15f,-21.65f,-17.68f,-12.50f, -6.47f,
      0.00f,  6.47f, 12.50f, 17.68f, 21.65f, 24.15f
  }
};

static const float sShapeY[SHAPE_COUNT][SHAPE_PT_CNT] = {
  { /* 0: 正方形 */
    /* 边1 */ -25.00f,-16.67f, -8.33f,  0.00f,  8.33f, 16.67f,
    /* 边2 */  25.00f, 25.00f, 25.00f, 25.00f, 25.00f, 25.00f,
    /* 边3 */  25.00f, 16.67f,  8.33f,  0.00f, -8.33f,-16.67f,
    /* 边4 */ -25.00f,-25.00f,-25.00f,-25.00f,-25.00f,-25.00f
  },
  { /* 1: 等边三角形，步长y=±5.41（=43.30/8） */
    /* 边1 */  28.87f, 23.46f, 18.04f, 12.63f,  7.22f,  1.80f, -3.61f, -9.02f,
    /* 边2 */ -14.43f,-14.43f,-14.43f,-14.43f,-14.43f,-14.43f,-14.43f,-14.43f,
    /* 边3 */ -14.43f, -9.02f, -3.61f,  1.80f,  7.22f, 12.63f, 18.04f, 23.46f
  },
  { /* 2: 圆形 */
      0.00f,  6.47f, 12.50f, 17.68f, 21.65f, 24.15f,
     25.00f, 24.15f, 21.65f, 17.68f, 12.50f,  6.47f,
      0.00f, -6.47f,-12.50f,-17.68f,-21.65f,-24.15f,
    -25.00f,-24.15f,-21.65f,-17.68f,-12.50f, -6.47f
  }
};

static uint8_t  sShapeIdx    = 0U;
static uint8_t  sShapePtIdx  = 0U;
static uint32_t sLastKey3Cnt = 0U;

void user1TaskUpdata(void *argument){
	user1TaskInit();
	for(;;){
		user1TaskInfo.user1TaskCnt++;

		/* key[3] 每按一次切换形状（正方形→三角形→圆形→循环）。 */
		if(boardInfo.key[BOARD_KEY3].pressCount != sLastKey3Cnt) {
			sLastKey3Cnt = boardInfo.key[BOARD_KEY3].pressCount;
			sShapeIdx   = (uint8_t)((sShapeIdx + 1U) % SHAPE_COUNT);
			sShapePtIdx = 0U;
		}

		/* 奇数 cnt 切换采样点（State0/Pitch 执行前，与双状态控制对齐）。 */
		if(user1TaskInfo.user1TaskCnt % 2U == 1U) {
			sShapePtIdx        = (uint8_t)((sShapePtIdx + 1U) % SHAPE_PT_CNT);
			user1TaskInfo.xRef = sShapeX[sShapeIdx][sShapePtIdx];
			user1TaskInfo.yRef = sShapeY[sShapeIdx][sShapePtIdx];
		}
		FUNC_GAMBAL_GotoXY(user1TaskInfo.xRef, user1TaskInfo.yRef);

		if(user1TaskInfo.user1TaskCnt % 5 == 0){
			/* 10ms TASK*/

		}

		if(user1TaskInfo.user1TaskCnt % 25 == 0){
			/* 50ms TASK*/

		}

		if(user1TaskInfo.user1TaskCnt % 50 == 0){
			/* 100ms TASK*/
			__USER1_OledUpdata();
		}

		if(user1TaskInfo.user1TaskCnt % 100 == 0){
			/* 200ms TASK*/

		}

		if(user1TaskInfo.user1TaskCnt % 500 == 0){
			/* 1000ms TASK */

		}

		osDelay(2);
	}
}
