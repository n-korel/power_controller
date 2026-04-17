#ifndef POWER_TEST_HELPERS_H
#define POWER_TEST_HELPERS_H

/*
 * Shared helpers for power_manager / display sequencing host tests.
 *
 * Provides default mock implementations of adc_service + input_service +
 * fault_manager symbols that power_manager.c depends on, plus utilities
 * to introspect the HAL GPIO spy log recorded in Tests/stubs/hal_stubs.c.
 */

#include <stdint.h>
#include "stm32f0xx_hal.h"

/* ADC raw sample storage (14 channels as per config.h). Populated by tests
 * before/between power_manager_process() ticks. */
extern uint16_t mock_raw_avg[14];

/* Inputs consumed by power_manager (input_service substitutes) */
extern uint8_t mock_pgood;
extern uint8_t mock_sus_s3;

/* Accumulated OR of every flag passed to fault_set_flag() since reset */
extern uint32_t fault_flags_set;

/* Reset mocks (ADC, pgood=1, sus_s3=1, fault_flags=0) and clear the HAL
 * GPIO spy log. Call in setUp() of every test. */
void pth_reset(void);

/* Return the *last* recorded HAL_GPIO_WritePin state for (port,pin).
 * Returns 1 and writes result to *out if found, 0 if never written. */
int pth_last_gpio_write(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState *out);

/* Count of HAL_GPIO_WritePin calls for (port,pin) since pth_reset */
uint32_t pth_gpio_write_count(GPIO_TypeDef *port, uint16_t pin);

/* Index of first/last write for (port,pin) in hal_gpio_log[], or -1. */
int pth_first_write_idx(GPIO_TypeDef *port, uint16_t pin);
int pth_last_write_idx(GPIO_TypeDef *port, uint16_t pin);

#endif /* POWER_TEST_HELPERS_H */
