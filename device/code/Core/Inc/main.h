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
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "message.h"
#include "api.h"
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
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define B1_EXTI_IRQn EXTI4_15_IRQn
#define LED_BAT_25_Pin GPIO_PIN_0
#define LED_BAT_25_GPIO_Port GPIOC
#define LED_BAT_50_Pin GPIO_PIN_1
#define LED_BAT_50_GPIO_Port GPIOC
#define LED_BAT_75_Pin GPIO_PIN_2
#define LED_BAT_75_GPIO_Port GPIOC
#define LED_BAT_100_Pin GPIO_PIN_3
#define LED_BAT_100_GPIO_Port GPIOC
#define LED_BT_Pin GPIO_PIN_6
#define LED_BT_GPIO_Port GPIOC
#define LED_GNSS_R_Pin GPIO_PIN_7
#define LED_GNSS_R_GPIO_Port GPIOC
#define LED_GNSS_G_Pin GPIO_PIN_8
#define LED_GNSS_G_GPIO_Port GPIOC
#define LED_CELL_R_Pin GPIO_PIN_9
#define LED_CELL_R_GPIO_Port GPIOC
#define LED_CELL_G_Pin GPIO_PIN_10
#define LED_CELL_G_GPIO_Port GPIOC
#define STATUS_BTN_Pin GPIO_PIN_11
#define STATUS_BTN_GPIO_Port GPIOC
#define STATUS_BTN_EXTI_IRQn EXTI4_15_IRQn
#define MCO_Pin GPIO_PIN_0
#define MCO_GPIO_Port GPIOF
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

extern volatile API_Status api_status;
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
