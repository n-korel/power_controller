#include "stm32f0xx_hal.h"

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

/* ===== GPIO write spy ===== */
gpio_write_record_t hal_gpio_log[HAL_STUB_GPIO_LOG_SIZE];
uint32_t hal_gpio_log_count;

/* ===== GPIO read map (configurable per-pin return values) ===== */
gpio_read_map_entry_t hal_gpio_read_map[HAL_STUB_READ_PIN_MAP_SIZE];
uint32_t hal_gpio_read_map_count;

void hal_stub_reset(void)
{
    hal_gpio_log_count = 0;
    hal_gpio_read_map_count = 0;
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
    if (hal_gpio_log_count < HAL_STUB_GPIO_LOG_SIZE) {
        hal_gpio_log[hal_gpio_log_count].port  = GPIOx;
        hal_gpio_log[hal_gpio_log_count].pin   = GPIO_Pin;
        hal_gpio_log[hal_gpio_log_count].state  = PinState;
        hal_gpio_log_count++;
    }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    for (uint32_t i = 0; i < hal_gpio_read_map_count; i++) {
        if (hal_gpio_read_map[i].port == GPIOx && hal_gpio_read_map[i].pin == GPIO_Pin)
            return hal_gpio_read_map[i].value;
    }
    return GPIO_PIN_RESET;
}

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size)
{
    (void)huart; (void)pData; (void)Size;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size)
{
    (void)huart; (void)pData; (void)Size;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t Channel)
{
    (void)htim; (void)Channel;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Unlock(void)  { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void)    { return HAL_OK; }

HAL_StatusTypeDef HAL_FLASH_Program(uint32_t TypeProgram, uint32_t Address, uint64_t Data)
{
    (void)TypeProgram; (void)Address; (void)Data;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *pEraseInit, uint32_t *PageError)
{
    (void)pEraseInit;
    if (PageError) *PageError = 0xFFFFFFFF;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *h)
{
    (void)h;
    return HAL_OK;
}

void Error_Handler(void) {}
