#ifndef __MAIN_H
#define __MAIN_H

#include "stm32f0xx_hal.h"

/* ===== GPIO port stubs (unique addresses for spy identification) ===== */
extern GPIO_TypeDef stub_gpioa, stub_gpiob, stub_gpioc, stub_gpiod, stub_gpiof;
#define GPIOA (&stub_gpioa)
#define GPIOB (&stub_gpiob)
#define GPIOC (&stub_gpioc)
#define GPIOD (&stub_gpiod)
#define GPIOF (&stub_gpiof)

/* ===== Pin definitions (mirror Core/Inc/main.h) ===== */
#define RSTBTN_Pin                GPIO_PIN_13
#define RSTBTN_GPIO_Port          GPIOC
#define PWRBTN_Pin                GPIO_PIN_14
#define PWRBTN_GPIO_Port          GPIOC
#define SUS_S3_Pin                GPIO_PIN_15
#define SUS_S3_GPIO_Port          GPIOC

#define LCD_CURRENT_M_Pin         GPIO_PIN_0
#define LCD_CURRENT_M_GPIO_Port   GPIOA
#define BACKLIGHT_CURRENT_M_Pin   GPIO_PIN_1
#define BACKLIGHT_CURRENT_M_GPIO_Port GPIOA
#define SCALER_CURRENT_M_Pin      GPIO_PIN_4
#define SCALER_CURRENT_M_GPIO_Port GPIOA
#define AUDIO_L_CURRENT_M_Pin     GPIO_PIN_5
#define AUDIO_L_CURRENT_M_GPIO_Port GPIOA
#define AUDIO_R_CURRENT_M_Pin     GPIO_PIN_6
#define AUDIO_R_CURRENT_M_GPIO_Port GPIOA
#define LCD_POWER_M_Pin           GPIO_PIN_7
#define LCD_POWER_M_GPIO_Port     GPIOA

#define BACKLIGHT_POWER_M_Pin     GPIO_PIN_0
#define BACKLIGHT_POWER_M_GPIO_Port GPIOB
#define SCALER_POWER_M_Pin        GPIO_PIN_1
#define SCALER_POWER_M_GPIO_Port  GPIOB
#define POWER_TOUCH_Pin           GPIO_PIN_2
#define POWER_TOUCH_GPIO_Port     GPIOB

#define IN_5_Pin                  GPIO_PIN_10
#define IN_5_GPIO_Port            GPIOB
#define IN_4_Pin                  GPIO_PIN_11
#define IN_4_GPIO_Port            GPIOB
#define IN_3_Pin                  GPIO_PIN_12
#define IN_3_GPIO_Port            GPIOB
#define IN_2_Pin                  GPIO_PIN_13
#define IN_2_GPIO_Port            GPIOB
#define IN_1_Pin                  GPIO_PIN_14
#define IN_1_GPIO_Port            GPIOB
#define IN_0_Pin                  GPIO_PIN_15
#define IN_0_GPIO_Port            GPIOB

#define MUTE_Pin                  GPIO_PIN_6
#define MUTE_GPIO_Port            GPIOC
#define FAULTZ_Pin                GPIO_PIN_7
#define FAULTZ_GPIO_Port          GPIOC
#define SDZ_Pin                   GPIO_PIN_8
#define SDZ_GPIO_Port             GPIOC
#define POWER_AUDIO_Pin           GPIO_PIN_9
#define POWER_AUDIO_GPIO_Port     GPIOC

#define PGOOD_Pin                 GPIO_PIN_8
#define PGOOD_GPIO_Port           GPIOA

#define BACKLIGHT_ON_Pin          GPIO_PIN_15
#define BACKLIGHT_ON_GPIO_Port    GPIOA
#define LCD_POWER_ON_Pin          GPIO_PIN_4
#define LCD_POWER_ON_GPIO_Port    GPIOB
#define SCALER_POWER_ON_Pin       GPIO_PIN_5
#define SCALER_POWER_ON_GPIO_Port GPIOB
#define POWER_ETH2_Pin            GPIO_PIN_6
#define POWER_ETH2_GPIO_Port      GPIOB
#define POWER_ETH1_Pin            GPIO_PIN_7
#define POWER_ETH1_GPIO_Port      GPIOB
#define RST_CH7511B_Pin           GPIO_PIN_8
#define RST_CH7511B_GPIO_Port     GPIOB
#define BL_PWM_Pin                GPIO_PIN_9
#define BL_PWM_GPIO_Port          GPIOB

#define TEMP0_M_Pin               GPIO_PIN_4
#define TEMP0_M_GPIO_Port         GPIOC
#define TEMP1_M_Pin               GPIO_PIN_5
#define TEMP1_M_GPIO_Port         GPIOC

#define V_24_M_Pin                GPIO_PIN_0
#define V_24_M_GPIO_Port          GPIOC
#define V_12_M_Pin                GPIO_PIN_1
#define V_12_M_GPIO_Port          GPIOC
#define V_5_M_Pin                 GPIO_PIN_2
#define V_5_M_GPIO_Port           GPIOC
#define V_3_3_M_Pin               GPIO_PIN_3
#define V_3_3_M_GPIO_Port         GPIOC

#endif /* __MAIN_H */
