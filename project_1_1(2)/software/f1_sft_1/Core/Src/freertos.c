/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "task_system.h"
#include "task_user1.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for userTask_1 */
osThreadId_t userTask_1Handle;
const osThreadAttr_t userTask_1_attributes = {
  .name = "userTask_1",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for userTask_2 */
osThreadId_t userTask_2Handle;
const osThreadAttr_t userTask_2_attributes = {
  .name = "userTask_2",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void systemTaskUpdata(void *argument);
void user1TaskUpdata(void *argument);
void user2TaskUpdata(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(systemTaskUpdata, NULL, &defaultTask_attributes);

  /* creation of userTask_1 */
  userTask_1Handle = osThreadNew(user1TaskUpdata, NULL, &userTask_1_attributes);

  /* creation of userTask_2 */
  userTask_2Handle = osThreadNew(user2TaskUpdata, NULL, &userTask_2_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_systemTaskUpdata */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_systemTaskUpdata */
__weak void systemTaskUpdata(void *argument)
{
  /* USER CODE BEGIN systemTaskUpdata */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END systemTaskUpdata */
}

/* USER CODE BEGIN Header_user1TaskUpdata */
/**
* @brief Function implementing the userTask_1 thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_user1TaskUpdata */
__weak void user1TaskUpdata(void *argument)
{
  /* USER CODE BEGIN user1TaskUpdata */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END user1TaskUpdata */
}

/* USER CODE BEGIN Header_user2TaskUpdata */
/**
* @brief Function implementing the userTask_2 thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_user2TaskUpdata */
__weak void user2TaskUpdata(void *argument)
{
  /* USER CODE BEGIN user2TaskUpdata */
  /* Infinite loop */
  for(;;)
  {
    osDelay(2);
  }
  /* USER CODE END user2TaskUpdata */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

