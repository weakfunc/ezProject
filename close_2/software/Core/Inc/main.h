/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define DIR_M5_Pin GPIO_PIN_13
#define DIR_M5_GPIO_Port GPIOC
#define DIR_M4_Pin GPIO_PIN_14
#define DIR_M4_GPIO_Port GPIOC
#define DIR_M3_Pin GPIO_PIN_15
#define DIR_M3_GPIO_Port GPIOC
#define ECHO_Pin GPIO_PIN_3
#define ECHO_GPIO_Port GPIOA
#define ECHO_EXTI_IRQn EXTI3_IRQn
#define KEY1_Pin GPIO_PIN_4
#define KEY1_GPIO_Port GPIOA
#define KEY2_Pin GPIO_PIN_5
#define KEY2_GPIO_Port GPIOA
#define LCD_Pin GPIO_PIN_6
#define LCD_GPIO_Port GPIOA
#define LCDA7_Pin GPIO_PIN_7
#define LCDA7_GPIO_Port GPIOA
#define LCDB0_Pin GPIO_PIN_0
#define LCDB0_GPIO_Port GPIOB
#define LCDB1_Pin GPIO_PIN_1
#define LCDB1_GPIO_Port GPIOB
#define LCDB10_Pin GPIO_PIN_10
#define LCDB10_GPIO_Port GPIOB
#define LCDB11_Pin GPIO_PIN_11
#define LCDB11_GPIO_Port GPIOB
#define DIR_M2_Pin GPIO_PIN_12
#define DIR_M2_GPIO_Port GPIOB
#define DIR_M1_Pin GPIO_PIN_14
#define DIR_M1_GPIO_Port GPIOB
#define BEEF_Pin GPIO_PIN_8
#define BEEF_GPIO_Port GPIOA
#define FBD4_B_Pin GPIO_PIN_11
#define FBD4_B_GPIO_Port GPIOA
#define FBD4_B_EXTI_IRQn EXTI15_10_IRQn
#define FBD4_A_Pin GPIO_PIN_12
#define FBD4_A_GPIO_Port GPIOA
#define FBD3_B_Pin GPIO_PIN_15
#define FBD3_B_GPIO_Port GPIOA
#define FBD3_B_EXTI_IRQn EXTI15_10_IRQn
#define FBD3_A_Pin GPIO_PIN_3
#define FBD3_A_GPIO_Port GPIOB
#define TRIG_Pin GPIO_PIN_4
#define TRIG_GPIO_Port GPIOB
#define FBD5_A_Pin GPIO_PIN_5
#define FBD5_A_GPIO_Port GPIOB
#define FBD5_A_EXTI_IRQn EXTI9_5_IRQn
#define FBD2_B_Pin GPIO_PIN_6
#define FBD2_B_GPIO_Port GPIOB
#define FBD2_B_EXTI_IRQn EXTI9_5_IRQn
#define FBD2_A_Pin GPIO_PIN_7
#define FBD2_A_GPIO_Port GPIOB
#define FBD1_B_Pin GPIO_PIN_8
#define FBD1_B_GPIO_Port GPIOB
#define FBD1_B_EXTI_IRQn EXTI9_5_IRQn
#define FBD1_A_Pin GPIO_PIN_9
#define FBD1_A_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
