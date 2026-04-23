/*
 * Unit tests: main-loop runtime order (Rules_POWER.md #48-#50)
 *
 * Goal: enforce behavioral ordering of app_step() calls and that IWDG refresh
 * is NOT part of app_step() (must remain in main loop only).
 */
#include "unity.h"
#include "stm32f0xx_hal.h"

#include "app.h"

typedef enum {
    CALL_UART_PROCESS = 1,
    CALL_ADC_PROCESS,
    CALL_INPUT_PROCESS,
    CALL_POWER_PROCESS,
    CALL_FAULT_PROCESS,
    CALL_BOOTLOADER_PROCESS
} app_call_id_t;

static app_call_id_t app_call_log[32];
static uint32_t app_call_log_count;
static uint16_t injected_fault_flags;

static void app_call_log_reset(void)
{
    app_call_log_count = 0;
    hal_stub_reset();
    injected_fault_flags = 0;
}

static void app_log(app_call_id_t id)
{
    if (app_call_log_count < (uint32_t)(sizeof(app_call_log) / sizeof(app_call_log[0]))) {
        app_call_log[app_call_log_count++] = id;
    }
}

/* ===== Stubs for app_step() dependencies ===== */
void uart_protocol_process(void) { app_log(CALL_UART_PROCESS); }
void adc_service_process(void)   { app_log(CALL_ADC_PROCESS); }
void input_service_process(void) { app_log(CALL_INPUT_PROCESS); }
void power_manager_process(void) { app_log(CALL_POWER_PROCESS); }
void fault_manager_process(void) { app_log(CALL_FAULT_PROCESS); }
void bootloader_process(void)    { app_log(CALL_BOOTLOADER_PROCESS); }
uint16_t fault_get_flags(void)   { return injected_fault_flags; }

/* Ensure app_init() dependencies are link-satisfied (not under test here). */
void power_safe_state(void) {}
void adc_service_init(void) {}
void input_service_init(void) {}
void power_manager_init(void) {}
void fault_manager_init(void) {}
void flash_cal_load(void) {}
void uart_protocol_init(void) {}
void power_startup_begin(void) {}
void fault_set_flag(uint16_t flag) { (void)flag; }

uint16_t *adc_get_dma_buf(void)
{
    static uint16_t buf[14];
    return buf;
}

/* Provide the global systick used across the firmware. */
volatile uint32_t systick_ms;

void setUp(void)
{
    app_call_log_reset();
    systick_ms = 1000;
}

void tearDown(void) {}

static void assert_app_order(const app_call_id_t *exp, uint32_t n)
{
    TEST_ASSERT_EQUAL_UINT32(n, app_call_log_count);
    for (uint32_t i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_INT(exp[i], app_call_log[i]);
    }
}

void test_app_step_runtime_order_matches_contract(void)
{
    app_step();

    const app_call_id_t exp[] = {
        CALL_UART_PROCESS,
        CALL_ADC_PROCESS,
        CALL_INPUT_PROCESS,
        CALL_POWER_PROCESS,
        CALL_FAULT_PROCESS,
        CALL_BOOTLOADER_PROCESS
    };
    assert_app_order(exp, (uint32_t)(sizeof(exp) / sizeof(exp[0])));

    /* Rules_POWER.md #48-#50: IWDG refresh is not allowed from app_step() */
    for (uint32_t i = 0; i < hal_call_log_count; i++) {
        TEST_ASSERT_NOT_EQUAL(HAL_CALL_IWDG_REFRESH, hal_call_log[i].id);
    }
}

void test_fault_short_circuits_after_fault_manager(void)
{
    injected_fault_flags = 0x0001U;

    app_step();

    const app_call_id_t exp[] = {
        CALL_UART_PROCESS,
        CALL_ADC_PROCESS,
        CALL_INPUT_PROCESS,
        CALL_POWER_PROCESS,
        CALL_FAULT_PROCESS
    };
    assert_app_order(exp, (uint32_t)(sizeof(exp) / sizeof(exp[0])));
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_app_step_runtime_order_matches_contract);
    RUN_TEST(test_fault_short_circuits_after_fault_manager);
    return UNITY_END();
}

