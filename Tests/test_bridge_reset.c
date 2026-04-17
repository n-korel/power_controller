/*
 * Unit tests: RST_CH7511B# bridge reset pulse — Rules_POWER.md §13.
 *
 *   - Rejected unless both SCALER and LCD are ON
 *   - Rejected while a display sequencing SM is active
 *   - Rejected while a previous pulse is still in flight
 *   - Pulse width = BRIDGE_RST_PULSE_MS (10 ms)
 */
#include "unity.h"
#include "config.h"
#include "power_test_helpers.h"
#include "main.h"
#include "tim.h"

#include "power_manager.c"

static void tick_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        systick_ms++;
        power_manager_process();
    }
}

void setUp(void)
{
    pth_reset();
    power_manager_init();
}

void tearDown(void) {}

/* ===== Rejection conditions ===== */

void test_reset_bridge_rejected_without_scaler_lcd(void)
{
    power_state = 0;

    power_reset_bridge();

    TEST_ASSERT_EQUAL_UINT32(0,
        pth_gpio_write_count(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin));
    TEST_ASSERT_EQUAL_UINT8(0, bridge_rst_active);
}

void test_reset_bridge_rejected_with_scaler_only(void)
{
    power_state = DOM_SCALER;

    power_reset_bridge();

    TEST_ASSERT_EQUAL_UINT32(0,
        pth_gpio_write_count(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin));
    TEST_ASSERT_EQUAL_UINT8(0, bridge_rst_active);
}

void test_reset_bridge_rejected_during_dseq(void)
{
    power_state = DOM_SCALER | DOM_LCD;
    dseq        = DSEQ_UP_WAIT_SCALER;

    power_reset_bridge();

    TEST_ASSERT_EQUAL_UINT32(0,
        pth_gpio_write_count(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin));
    TEST_ASSERT_EQUAL_UINT8(0, bridge_rst_active);
}

/* ===== Pulse timing ===== */

void test_reset_bridge_pulse_is_10ms(void)
{
    power_state = DOM_SCALER | DOM_LCD;

    power_reset_bridge();

    /* Immediately: RST asserted LOW, pulse in progress */
    GPIO_PinState st;
    TEST_ASSERT_EQUAL_UINT32(1,
        pth_gpio_write_count(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin));
    TEST_ASSERT_TRUE(pth_last_gpio_write(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_EQUAL_UINT8(1, bridge_rst_active);

    /* Before 10 ms: still asserted */
    tick_ms(BRIDGE_RST_PULSE_MS - 1);
    TEST_ASSERT_EQUAL_UINT32(1,
        pth_gpio_write_count(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin));
    TEST_ASSERT_EQUAL_UINT8(1, bridge_rst_active);

    /* Past 10 ms: released HIGH, SM cleared */
    tick_ms(2);
    TEST_ASSERT_TRUE(pth_last_gpio_write(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_EQUAL_UINT32(2,
        pth_gpio_write_count(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin));
    TEST_ASSERT_EQUAL_UINT8(0, bridge_rst_active);
}

/* ===== Reentrancy ===== */

void test_reset_bridge_ignored_if_already_active(void)
{
    power_state = DOM_SCALER | DOM_LCD;

    power_reset_bridge();
    tick_ms(5);  /* mid-pulse */
    uint32_t timer_before = bridge_rst_timer;

    power_reset_bridge();  /* second call, must be ignored */

    TEST_ASSERT_EQUAL_UINT32(1,
        pth_gpio_write_count(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin));
    TEST_ASSERT_EQUAL_UINT32(timer_before, bridge_rst_timer);

    /* Pulse must still release on the ORIGINAL 10 ms boundary, not extended */
    tick_ms(6);
    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_EQUAL_UINT8(0, bridge_rst_active);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reset_bridge_rejected_without_scaler_lcd);
    RUN_TEST(test_reset_bridge_rejected_with_scaler_only);
    RUN_TEST(test_reset_bridge_rejected_during_dseq);
    RUN_TEST(test_reset_bridge_pulse_is_10ms);
    RUN_TEST(test_reset_bridge_ignored_if_already_active);
    return UNITY_END();
}
