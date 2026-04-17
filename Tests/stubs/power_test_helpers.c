#include "power_test_helpers.h"
#include "config.h"
#include "stm32f0xx_hal.h"
#include <string.h>

/* Forward decl: resolved at link time from power_manager.c (included by the
 * test TU). The stub below mirrors fault_manager's apply_fault_policy just
 * enough to unblock sequencing tests that want to observe the same post-fault
 * power_state the real fault_manager would produce in firmware. */
void power_force_off_domains(uint16_t domain_mask);

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

    /* Mirror fault_manager::apply_fault_policy (Rules §7.3) so that sequencing
     * tests observe power_state cleared by the same path production uses. */
    const uint16_t kill_all_display =
        FAULT_SCALER | FAULT_SEQ_ABORT | FAULT_PGOOD_LOST |
        FAULT_V24_RANGE | FAULT_V12_RANGE | FAULT_V5_RANGE |
        FAULT_V3V3_RANGE | FAULT_INTERNAL;

    if (flag & kill_all_display) {
        power_force_off_domains(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT);
    }
    if (flag & FAULT_LCD) {
        power_force_off_domains(DOM_LCD | DOM_BACKLIGHT);
    }
    if (flag & FAULT_BACKLIGHT) {
        power_force_off_domains(DOM_BACKLIGHT);
    }
    if (flag & (FAULT_AUDIO | FAULT_AMP_FAULTZ)) {
        power_force_off_domains(DOM_AUDIO);
    }
    if (flag & (FAULT_PGOOD_LOST | FAULT_V24_RANGE | FAULT_V12_RANGE |
                FAULT_V5_RANGE | FAULT_V3V3_RANGE | FAULT_INTERNAL)) {
        power_force_off_domains(DOM_AUDIO | DOM_ETH1 | DOM_ETH2 | DOM_TOUCH);
    }
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

uint32_t pth_gpio_high_count(GPIO_TypeDef *port, uint16_t pin)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < hal_gpio_log_count; i++) {
        if (hal_gpio_log[i].port == port &&
            hal_gpio_log[i].pin  == pin  &&
            hal_gpio_log[i].state == GPIO_PIN_SET) n++;
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
