#include "power_test_helpers.h"
#include "stm32f0xx_hal.h"
#include <string.h>

/* ===== Storage for mocks consumed by power_manager.c ===== */
uint16_t mock_raw_avg[14];
uint8_t  mock_pgood;
uint8_t  mock_sus_s3;
uint32_t fault_flags_set;

/* ===== Global systick referenced across the firmware ===== */
volatile uint32_t systick_ms;

/* ===== Substitutes for adc_service / input_service / fault_manager ===== */
uint16_t adc_get_raw_avg(uint8_t idx)
{
    return (idx < 14) ? mock_raw_avg[idx] : 0;
}

uint8_t input_get_pgood(void)  { return mock_pgood;  }
uint8_t input_get_sus_s3(void) { return mock_sus_s3; }

void fault_set_flag(uint16_t flag)
{
    fault_flags_set |= flag;
}

/* ===== Test utilities ===== */
void pth_reset(void)
{
    memset(mock_raw_avg, 0, sizeof(mock_raw_avg));
    mock_pgood       = 1;
    mock_sus_s3      = 1;
    fault_flags_set  = 0;
    systick_ms       = 1000;
    hal_stub_reset();
}

int pth_last_gpio_write(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState *out)
{
    for (int32_t i = (int32_t)hal_gpio_log_count - 1; i >= 0; i--) {
        if (hal_gpio_log[i].port == port && hal_gpio_log[i].pin == pin) {
            if (out) *out = hal_gpio_log[i].state;
            return 1;
        }
    }
    return 0;
}

uint32_t pth_gpio_write_count(GPIO_TypeDef *port, uint16_t pin)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < hal_gpio_log_count; i++) {
        if (hal_gpio_log[i].port == port && hal_gpio_log[i].pin == pin) n++;
    }
    return n;
}

int pth_first_write_idx(GPIO_TypeDef *port, uint16_t pin)
{
    for (uint32_t i = 0; i < hal_gpio_log_count; i++) {
        if (hal_gpio_log[i].port == port && hal_gpio_log[i].pin == pin) return (int)i;
    }
    return -1;
}

int pth_last_write_idx(GPIO_TypeDef *port, uint16_t pin)
{
    for (int32_t i = (int32_t)hal_gpio_log_count - 1; i >= 0; i--) {
        if (hal_gpio_log[i].port == port && hal_gpio_log[i].pin == pin) return (int)i;
    }
    return -1;
}
