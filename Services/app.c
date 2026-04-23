#include "app.h"

#include "main.h"
#include "adc.h"
#include "iwdg.h"
#include "tim.h"

#include "adc_service.h"
#include "input_service.h"
#include "uart_protocol.h"
#include "power_manager.h"
#include "fault_manager.h"
#include "flash_cal.h"
#include "bootloader.h"
#include "config.h"

void app_init(void)
{
    power_safe_state();

    adc_service_init();
    input_service_init();
    power_manager_init();
    fault_manager_init();
    flash_cal_load();

    if (HAL_ADCEx_Calibration_Start(&hadc) != HAL_OK) {
        fault_set_flag(FAULT_INTERNAL);
        return;
    }
    if (HAL_ADC_Start_DMA(&hadc, (uint32_t *)adc_get_dma_buf(), ADC_CHANNEL_COUNT) != HAL_OK) {
        fault_set_flag(FAULT_INTERNAL);
        return;
    }
    if (HAL_TIM_PWM_Start(&htim17, TIM_CHANNEL_1) != HAL_OK) {
        fault_set_flag(FAULT_INTERNAL);
        return;
    }

    uart_protocol_init();

    power_startup_begin();
}

void app_step(void)
{
    uart_protocol_process();
    adc_service_process();
    input_service_process();
    power_manager_process();
    fault_manager_process();
    bootloader_process();

    if (fault_get_flags() != 0U) {
        return;
    }
}

