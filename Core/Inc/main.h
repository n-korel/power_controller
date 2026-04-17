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
#include "stm32f0xx_hal.h"

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
#define RSTBTN_Pin GPIO_PIN_13
#define RSTBTN_GPIO_Port GPIOC
#define PWRBTN_Pin GPIO_PIN_14
#define PWRBTN_GPIO_Port GPIOC
#define SUS_S3_Pin GPIO_PIN_15
#define SUS_S3_GPIO_Port GPIOC
#define V_24_M_Pin GPIO_PIN_0
#define V_24_M_GPIO_Port GPIOC
#define V_12_M_Pin GPIO_PIN_1
#define V_12_M_GPIO_Port GPIOC
#define V_5_M_Pin GPIO_PIN_2
#define V_5_M_GPIO_Port GPIOC
#define V_3_3_M_Pin GPIO_PIN_3
#define V_3_3_M_GPIO_Port GPIOC
#define LCD_CURRENT_M_Pin GPIO_PIN_0
#define LCD_CURRENT_M_GPIO_Port GPIOA
#define BACKLIGHT_CURRENT_M_Pin GPIO_PIN_1
#define BACKLIGHT_CURRENT_M_GPIO_Port GPIOA
#define UART_DEBUG_TX_Pin GPIO_PIN_2
#define UART_DEBUG_TX_GPIO_Port GPIOA
#define UART_DEBUG_RX_Pin GPIO_PIN_3
#define UART_DEBUG_RX_GPIO_Port GPIOA
#define Analog_Pin GPIO_PIN_4
#define Analog_GPIO_Port GPIOF
#define AnalogF5_Pin GPIO_PIN_5
#define AnalogF5_GPIO_Port GPIOF
#define SCALER_CURRENT_M_Pin GPIO_PIN_4
#define SCALER_CURRENT_M_GPIO_Port GPIOA
#define AUDIO_L_CURRENT_M_Pin GPIO_PIN_5
#define AUDIO_L_CURRENT_M_GPIO_Port GPIOA
#define AUDIO_R_CURRENT_M_Pin GPIO_PIN_6
#define AUDIO_R_CURRENT_M_GPIO_Port GPIOA
#define LCD_POWER_M_Pin GPIO_PIN_7
#define LCD_POWER_M_GPIO_Port GPIOA
#define TEMP0_M_Pin GPIO_PIN_4
#define TEMP0_M_GPIO_Port GPIOC
#define TEMP1_M_Pin GPIO_PIN_5
#define TEMP1_M_GPIO_Port GPIOC
#define BACKLIGHT_POWER_M_Pin GPIO_PIN_0
#define BACKLIGHT_POWER_M_GPIO_Port GPIOB
#define SCALER_POWER_M_Pin GPIO_PIN_1
#define SCALER_POWER_M_GPIO_Port GPIOB
#define POWER_TOUCH_Pin GPIO_PIN_2
#define POWER_TOUCH_GPIO_Port GPIOB
#define IN_5_Pin GPIO_PIN_10
#define IN_5_GPIO_Port GPIOB
#define IN_4_Pin GPIO_PIN_11
#define IN_4_GPIO_Port GPIOB
#define IN_3_Pin GPIO_PIN_12
#define IN_3_GPIO_Port GPIOB
#define IN_2_Pin GPIO_PIN_13
#define IN_2_GPIO_Port GPIOB
#define IN_1_Pin GPIO_PIN_14
#define IN_1_GPIO_Port GPIOB
#define IN_0_Pin GPIO_PIN_15
#define IN_0_GPIO_Port GPIOB
#define MUTE_Pin GPIO_PIN_6
#define MUTE_GPIO_Port GPIOC
#define FAULTZ_Pin GPIO_PIN_7
#define FAULTZ_GPIO_Port GPIOC
#define SDZ_Pin GPIO_PIN_8
#define SDZ_GPIO_Port GPIOC
#define POWER_AUDIO_Pin GPIO_PIN_9
#define POWER_AUDIO_GPIO_Port GPIOC
#define PGOOD_Pin GPIO_PIN_8
#define PGOOD_GPIO_Port GPIOA
#define UART0_TX_Pin GPIO_PIN_9
#define UART0_TX_GPIO_Port GPIOA
#define UART0_RX_Pin GPIO_PIN_10
#define UART0_RX_GPIO_Port GPIOA
#define OUT_0_Pin GPIO_PIN_11
#define OUT_0_GPIO_Port GPIOA
#define OUT_1_Pin GPIO_PIN_12
#define OUT_1_GPIO_Port GPIOA
#define OUT_2_Pin GPIO_PIN_6
#define OUT_2_GPIO_Port GPIOF
#define OUT_3_Pin GPIO_PIN_7
#define OUT_3_GPIO_Port GPIOF
#define BACKLIGHT_ON_Pin GPIO_PIN_15
#define BACKLIGHT_ON_GPIO_Port GPIOA
#define AnalogC10_Pin GPIO_PIN_10
#define AnalogC10_GPIO_Port GPIOC
#define AnalogC11_Pin GPIO_PIN_11
#define AnalogC11_GPIO_Port GPIOC
#define AnalogC12_Pin GPIO_PIN_12
#define AnalogC12_GPIO_Port GPIOC
#define AnalogD2_Pin GPIO_PIN_2
#define AnalogD2_GPIO_Port GPIOD
#define AnalogB3_Pin GPIO_PIN_3
#define AnalogB3_GPIO_Port GPIOB
#define LCD_POWER_ON_Pin GPIO_PIN_4
#define LCD_POWER_ON_GPIO_Port GPIOB
#define SCALER_POWER_ON_Pin GPIO_PIN_5
#define SCALER_POWER_ON_GPIO_Port GPIOB
#define POWER_ETH2_Pin GPIO_PIN_6
#define POWER_ETH2_GPIO_Port GPIOB
#define POWER_ETH1_Pin GPIO_PIN_7
#define POWER_ETH1_GPIO_Port GPIOB
#define RST_CH7511B_Pin GPIO_PIN_8
#define RST_CH7511B_GPIO_Port GPIOB
#define BL_PWM_Pin GPIO_PIN_9
#define BL_PWM_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
