/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "stdlib_common.h"

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
#define KEY_3_Pin GPIO_PIN_13
#define KEY_3_GPIO_Port GPIOC
#define RGB_G_Pin GPIO_PIN_14
#define RGB_G_GPIO_Port GPIOC
#define RGB_R_Pin GPIO_PIN_15
#define RGB_R_GPIO_Port GPIOC
#define USER_IO_ADC_Pin GPIO_PIN_0
#define USER_IO_ADC_GPIO_Port GPIOA
#define USER_IO_7_Pin GPIO_PIN_4
#define USER_IO_7_GPIO_Port GPIOA
#define USER_IO_8_Pin GPIO_PIN_5
#define USER_IO_8_GPIO_Port GPIOA
#define USER_IO_5_Pin GPIO_PIN_2
#define USER_IO_5_GPIO_Port GPIOB
#define USER_IO_4_Pin GPIO_PIN_12
#define USER_IO_4_GPIO_Port GPIOB
#define USER_IO_3_Pin GPIO_PIN_13
#define USER_IO_3_GPIO_Port GPIOB
#define KEY_2_Pin GPIO_PIN_14
#define KEY_2_GPIO_Port GPIOB
#define KEY_1_Pin GPIO_PIN_15
#define KEY_1_GPIO_Port GPIOB
#define USER_IO_2_Pin GPIO_PIN_8
#define USER_IO_2_GPIO_Port GPIOA
#define USER_IO_10_Pin GPIO_PIN_11
#define USER_IO_10_GPIO_Port GPIOA
#define USER_IO_9_Pin GPIO_PIN_12
#define USER_IO_9_GPIO_Port GPIOA
#define I2C_SDA_Pin GPIO_PIN_4
#define I2C_SDA_GPIO_Port GPIOB
#define I2C_SDL_Pin GPIO_PIN_5
#define I2C_SDL_GPIO_Port GPIOB
#define USER_IO_6_Pin GPIO_PIN_7
#define USER_IO_6_GPIO_Port GPIOB
#define USER_IO_1_Pin GPIO_PIN_8
#define USER_IO_1_GPIO_Port GPIOB
#define PWM_BUZZER_Pin GPIO_PIN_9
#define PWM_BUZZER_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
