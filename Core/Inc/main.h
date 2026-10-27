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
#include "stm32f4xx_hal.h"

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
#define RELAY1_Pin GPIO_PIN_2
#define RELAY1_GPIO_Port GPIOE
#define RELAY2_Pin GPIO_PIN_3
#define RELAY2_GPIO_Port GPIOE
#define INPUT1_Pin GPIO_PIN_4
#define INPUT1_GPIO_Port GPIOE
#define INPUT2_Pin GPIO_PIN_5
#define INPUT2_GPIO_Port GPIOE
#define INPUT3_Pin GPIO_PIN_6
#define INPUT3_GPIO_Port GPIOE
#define RS485_DE_Pin GPIO_PIN_2
#define RS485_DE_GPIO_Port GPIOA
#define FLASH_CS_Pin GPIO_PIN_4
#define FLASH_CS_GPIO_Port GPIOA
#define FLOW1_Pin GPIO_PIN_0
#define FLOW1_GPIO_Port GPIOB
#define FLOW1_EXTI_IRQn EXTI0_IRQn
#define FLOW2_Pin GPIO_PIN_1
#define FLOW2_GPIO_Port GPIOB
#define FLOW2_EXTI_IRQn EXTI1_IRQn
#define TFT_RESET_Pin GPIO_PIN_12
#define TFT_RESET_GPIO_Port GPIOB
#define TFT_DC_Pin GPIO_PIN_14
#define TFT_DC_GPIO_Port GPIOB
#define TFT_CS_Pin GPIO_PIN_8
#define TFT_CS_GPIO_Port GPIOD
#define TFT_LED_Pin GPIO_PIN_9
#define TFT_LED_GPIO_Port GPIOD
#define RF_VCC1_Pin GPIO_PIN_9
#define RF_VCC1_GPIO_Port GPIOC
#define RF_BUSY_Pin GPIO_PIN_8
#define RF_BUSY_GPIO_Port GPIOA
#define RF_DIO1_Pin GPIO_PIN_9
#define RF_DIO1_GPIO_Port GPIOA
#define RF_VCC2_Pin GPIO_PIN_10
#define RF_VCC2_GPIO_Port GPIOA
#define RF_NSS_Pin GPIO_PIN_15
#define RF_NSS_GPIO_Port GPIOA
#define RF_DIO2_Pin GPIO_PIN_0
#define RF_DIO2_GPIO_Port GPIOD
#define RF_NRST_Pin GPIO_PIN_1
#define RF_NRST_GPIO_Port GPIOD
#define BQ_INT_Pin GPIO_PIN_0
#define BQ_INT_GPIO_Port GPIOE
#define BQ_CE_Pin GPIO_PIN_1
#define BQ_CE_GPIO_Port GPIOE
#define LED_Y3_Pin GPIO_PIN_4
#define LED_Y3_GPIO_Port GPIOB
#define LED_Y4_Pin GPIO_PIN_3
#define LED_Y4_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
