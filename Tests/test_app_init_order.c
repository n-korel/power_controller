/*
 * Unit tests: app_init order contract
 *
 * Rules_POWER.md invariant 25:
 *   Safe state on startup is mandatory before processing any UART commands
 *   and before any power sequencing.
 *
 * This host test pins down the required init ordering:
 *   power_safe_state() must be called before uart_protocol_init().
 */
#include "unity.h"
#include "stm32f0xx_hal.h"
#include "config.h"

/* Bring in the app_init() implementation. */
#include "app.h"

static uint32_t seq;
static uint32_t order_power_safe_state;
static uint32_t order_uart_protocol_init;

/* ===== Stubs for dependencies of app.c ===== */
void power_safe_state(void)            { order_power_safe_state = ++seq; }
void adc_service_init(void)            { ++seq; }
void input_service_init(void)          { ++seq; }
void power_manager_init(void)          { ++seq; }
void fault_manager_init(void)          { ++seq; }
void flash_cal_load(void)              { ++seq; }
void uart_protocol_init(void)          { order_uart_protocol_init = ++seq; }
void power_startup_begin(void)         { ++seq; }
void fault_set_flag(uint16_t flag)     { (void)flag; ++seq; }

static uint16_t dma_buf_stub[ADC_CHANNEL_COUNT];
uint16_t *adc_get_dma_buf(void)        { return dma_buf_stub; }

void uart_protocol_process(void)       {}
void adc_service_process(void)         {}
void input_service_process(void)       {}
void power_manager_process(void)       {}
void fault_manager_process(void)       {}
void bootloader_process(void)          {}

/* HAL return codes are configurable via hal_stubs.c globals. */
extern void hal_stub_reset(void);
extern HAL_StatusTypeDef hal_stub_ret_adcex_calibration_start;
extern HAL_StatusTypeDef hal_stub_ret_adc_start_dma;
extern HAL_StatusTypeDef hal_stub_ret_tim_pwm_start;

void setUp(void)
{
    seq = 0;
    order_power_safe_state = 0;
    order_uart_protocol_init = 0;
    hal_stub_reset();
    hal_stub_ret_adcex_calibration_start = HAL_OK;
    hal_stub_ret_adc_start_dma = HAL_OK;
    hal_stub_ret_tim_pwm_start = HAL_OK;
}

void tearDown(void) {}

void test_app_init_calls_safe_state_before_uart_init(void)
{
    app_init();

    TEST_ASSERT_NOT_EQUAL_UINT32(0, order_power_safe_state);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, order_uart_protocol_init);
    TEST_ASSERT_EQUAL_UINT32(1, order_power_safe_state);
    TEST_ASSERT_TRUE(order_power_safe_state < order_uart_protocol_init);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_app_init_calls_safe_state_before_uart_init);
    return UNITY_END();
}

