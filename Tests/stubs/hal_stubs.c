#include "stm32f0xx_hal.h"
#include <string.h>

/* ===== GPIO port instances ===== */
GPIO_TypeDef stub_gpioa = { .tag = 0xA };
GPIO_TypeDef stub_gpiob = { .tag = 0xB };
GPIO_TypeDef stub_gpioc = { .tag = 0xC };
GPIO_TypeDef stub_gpiod = { .tag = 0xD };
GPIO_TypeDef stub_gpiof = { .tag = 0xF };

/* ===== Peripheral handles ===== */
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
TIM_HandleTypeDef  htim17;
IWDG_HandleTypeDef hiwdg;
ADC_HandleTypeDef  hadc;

/* ===== GPIO write spy ===== */
gpio_write_record_t hal_gpio_log[HAL_STUB_GPIO_LOG_SIZE];
uint32_t hal_gpio_log_count;

/* ===== GPIO read map (configurable per-pin return values) ===== */
gpio_read_map_entry_t hal_gpio_read_map[HAL_STUB_READ_PIN_MAP_SIZE];
uint32_t hal_gpio_read_map_count;

/* ===== Generic HAL call trace ===== */
hal_call_record_t hal_call_log[HAL_STUB_CALL_LOG_SIZE];
uint32_t hal_call_log_count;

/* ===== Configurable return codes ===== */
HAL_StatusTypeDef hal_stub_ret_uart_receive_it;
HAL_StatusTypeDef hal_stub_ret_uart_transmit_it;
HAL_StatusTypeDef hal_stub_ret_tim_pwm_start;
HAL_StatusTypeDef hal_stub_ret_iwdg_refresh;
HAL_StatusTypeDef hal_stub_ret_adcex_calibration_start;
HAL_StatusTypeDef hal_stub_ret_adc_start_dma;

/* ===== Cortex-M intrinsic spies ===== */
uint32_t hal_stub_nvic_reset_count;
uint32_t hal_stub_set_msp_count;
uint32_t hal_stub_set_msp_last_value;

static void hal_stub_log_call(hal_call_id_t id, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2)
{
    if (hal_call_log_count < HAL_STUB_CALL_LOG_SIZE) {
        hal_call_log[hal_call_log_count].id   = id;
        hal_call_log[hal_call_log_count].arg0 = arg0;
        hal_call_log[hal_call_log_count].arg1 = arg1;
        hal_call_log[hal_call_log_count].arg2 = arg2;
        hal_call_log_count++;
    }
}

void hal_stub_reset(void)
{
    hal_gpio_log_count = 0;
    hal_gpio_read_map_count = 0;
    hal_call_log_count = 0;
    hal_stub_nvic_reset_count = 0;
    hal_stub_set_msp_count = 0;
    hal_stub_set_msp_last_value = 0;
    hal_stub_ret_uart_receive_it = HAL_OK;
    hal_stub_ret_uart_transmit_it = HAL_OK;
    hal_stub_ret_tim_pwm_start = HAL_OK;
    hal_stub_ret_iwdg_refresh = HAL_OK;
    hal_stub_ret_adcex_calibration_start = HAL_OK;
    hal_stub_ret_adc_start_dma = HAL_OK;
}

void hal_stub_set_pin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState val)
{
    for (uint32_t i = 0; i < hal_gpio_read_map_count; i++) {
        if (hal_gpio_read_map[i].port == port && hal_gpio_read_map[i].pin == pin) {
            hal_gpio_read_map[i].value = val;
            return;
        }
    }
    if (hal_gpio_read_map_count < HAL_STUB_READ_PIN_MAP_SIZE) {
        hal_gpio_read_map[hal_gpio_read_map_count].port  = port;
        hal_gpio_read_map[hal_gpio_read_map_count].pin   = pin;
        hal_gpio_read_map[hal_gpio_read_map_count].value  = val;
        hal_gpio_read_map_count++;
    }
}

/* ===== HAL function stubs ===== */

void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
    hal_stub_log_call(HAL_CALL_GPIO_WRITE, (uintptr_t)GPIOx, (uintptr_t)GPIO_Pin, (uintptr_t)PinState);
    if (hal_gpio_log_count < HAL_STUB_GPIO_LOG_SIZE) {
        hal_gpio_log[hal_gpio_log_count].port  = GPIOx;
        hal_gpio_log[hal_gpio_log_count].pin   = GPIO_Pin;
        hal_gpio_log[hal_gpio_log_count].state  = PinState;
        hal_gpio_log_count++;
    }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    hal_stub_log_call(HAL_CALL_GPIO_READ, (uintptr_t)GPIOx, (uintptr_t)GPIO_Pin, 0);
    for (uint32_t i = 0; i < hal_gpio_read_map_count; i++) {
        if (hal_gpio_read_map[i].port == GPIOx && hal_gpio_read_map[i].pin == GPIO_Pin)
            return hal_gpio_read_map[i].value;
    }
    return GPIO_PIN_RESET;
}

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size)
{
    hal_stub_log_call(HAL_CALL_UART_RECEIVE_IT, (uintptr_t)huart, (uintptr_t)pData, (uintptr_t)Size);
    return hal_stub_ret_uart_receive_it;
}

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size)
{
    hal_stub_log_call(HAL_CALL_UART_TRANSMIT_IT, (uintptr_t)huart, (uintptr_t)pData, (uintptr_t)Size);
    return hal_stub_ret_uart_transmit_it;
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t Channel)
{
    hal_stub_log_call(HAL_CALL_TIM_PWM_START, (uintptr_t)htim, (uintptr_t)Channel, 0);
    return hal_stub_ret_tim_pwm_start;
}

HAL_StatusTypeDef HAL_FLASH_Unlock(void)
{
    hal_stub_log_call(HAL_CALL_FLASH_UNLOCK, 0, 0, 0);
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Lock(void)
{
    hal_stub_log_call(HAL_CALL_FLASH_LOCK, 0, 0, 0);
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Program(uint32_t TypeProgram, uintptr_t Address, uint64_t Data)
{
    hal_stub_log_call(HAL_CALL_FLASH_PROGRAM, (uintptr_t)TypeProgram, Address, (uintptr_t)Data);
    (void)TypeProgram;
    /* Host-test: Address points to a RAM buffer provided by the test.
     * flash_cal.c only writes WORD (32-bit). */
    uint32_t word = (uint32_t)Data;
    memcpy((void *)Address, &word, sizeof(word));
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *pEraseInit, uint32_t *PageError)
{
    hal_stub_log_call(HAL_CALL_FLASH_ERASE, (uintptr_t)pEraseInit, (uintptr_t)PageError, 0);
    if (pEraseInit) {
        /* Erase 128 bytes (enough for flash_cal_t) to 0xFF */
        memset((void *)pEraseInit->PageAddress, 0xFF, 128);
    }
    if (PageError) *PageError = 0xFFFFFFFF;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *h)
{
    hal_stub_log_call(HAL_CALL_IWDG_REFRESH, (uintptr_t)h, 0, 0);
    return hal_stub_ret_iwdg_refresh;
}

HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *hadc)
{
    hal_stub_log_call(HAL_CALL_ADCEX_CALIBRATION_START, (uintptr_t)hadc, 0, 0);
    return hal_stub_ret_adcex_calibration_start;
}

HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *hadc, uint32_t *pData, uint32_t Length)
{
    hal_stub_log_call(HAL_CALL_ADC_START_DMA, (uintptr_t)hadc, (uintptr_t)pData, (uintptr_t)Length);
    return hal_stub_ret_adc_start_dma;
}

void __set_MSP(uint32_t topOfMainStack)
{
    hal_stub_set_msp_count++;
    hal_stub_set_msp_last_value = topOfMainStack;
}

void NVIC_SystemReset(void)
{
    hal_stub_nvic_reset_count++;
}

void __DSB(void) {}

__attribute__((weak)) void Error_Handler(void) {}

/* Optional weak mock: tests can override with a strong definition. */
__attribute__((weak)) uint16_t fault_get_flags(void)
{
    return 0;
}
