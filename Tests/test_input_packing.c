/*
 * Unit tests: Input service — debounce and bit packing (Rules 4.5, 16)
 *
 * input_get_packed() layout (Rules 4.5 GET_STATUS):
 *   bit0..5 = IN0..IN5
 *   bit6    = PGOOD
 *   bit7    = Faultz
 *
 * Debounce: 20ms (DEBOUNCE_MS) — new state accepted only after stable for ≥20ms.
 */
#include "unity.h"
#include "config.h"

volatile uint32_t systick_ms;

#include "input_service.c"

static void set_all_pins(GPIO_PinState val)
{
    for (uint8_t i = 0; i < INPUT_COUNT; i++)
        hal_stub_set_pin(pins[i].port, pins[i].pin, val);
}

/* Drive all debounced inputs to match current raw GPIO state */
static void settle_debounce(void)
{
    input_service_process();
    systick_ms += DEBOUNCE_MS + 1;
    input_service_process();
}

void setUp(void)
{
    hal_stub_reset();
    systick_ms = 1000;
    set_all_pins(GPIO_PIN_RESET);
    input_service_init();
}

void tearDown(void) {}

/* ===== Bit packing layout (Rules 4.5) ===== */

void test_packed_all_zeros(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, input_get_packed());
}

void test_packed_all_ones(void)
{
    set_all_pins(GPIO_PIN_SET);
    settle_debounce();
    TEST_ASSERT_EQUAL_HEX8(0xFF, input_get_packed());
}

void test_packed_pgood_is_bit6(void)
{
    hal_stub_set_pin(PGOOD_GPIO_Port, PGOOD_Pin, GPIO_PIN_SET);
    settle_debounce();
    TEST_ASSERT_EQUAL_HEX8(0x40, input_get_packed());
}

void test_packed_faultz_is_bit7(void)
{
    hal_stub_set_pin(FAULTZ_GPIO_Port, FAULTZ_Pin, GPIO_PIN_SET);
    settle_debounce();
    TEST_ASSERT_EQUAL_HEX8(0x80, input_get_packed());
}

void test_packed_in0_is_bit0(void)
{
    hal_stub_set_pin(IN_0_GPIO_Port, IN_0_Pin, GPIO_PIN_SET);
    settle_debounce();
    TEST_ASSERT_EQUAL_HEX8(0x01, input_get_packed());
}

void test_packed_in5_is_bit5(void)
{
    hal_stub_set_pin(IN_5_GPIO_Port, IN_5_Pin, GPIO_PIN_SET);
    settle_debounce();
    TEST_ASSERT_EQUAL_HEX8(0x20, input_get_packed());
}

void test_packed_mixed_pattern(void)
{
    hal_stub_set_pin(PGOOD_GPIO_Port, PGOOD_Pin, GPIO_PIN_SET);
    hal_stub_set_pin(IN_0_GPIO_Port,  IN_0_Pin,  GPIO_PIN_SET);
    hal_stub_set_pin(IN_3_GPIO_Port,  IN_3_Pin,  GPIO_PIN_SET);
    settle_debounce();
    TEST_ASSERT_EQUAL_HEX8(0x49, input_get_packed());
}

/* ===== Individual getters ===== */

void test_get_pgood_returns_filtered(void)
{
    hal_stub_set_pin(PGOOD_GPIO_Port, PGOOD_Pin, GPIO_PIN_SET);
    settle_debounce();
    TEST_ASSERT_EQUAL_UINT8(1, input_get_pgood());
}

void test_get_sus_s3_returns_filtered(void)
{
    hal_stub_set_pin(SUS_S3_GPIO_Port, SUS_S3_Pin, GPIO_PIN_SET);
    settle_debounce();
    TEST_ASSERT_EQUAL_UINT8(1, input_get_sus_s3());
}

void test_get_faultz_returns_filtered(void)
{
    hal_stub_set_pin(FAULTZ_GPIO_Port, FAULTZ_Pin, GPIO_PIN_SET);
    settle_debounce();
    TEST_ASSERT_EQUAL_UINT8(1, input_get_faultz());
}

void test_get_in_bounds_check(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, input_get_in(6));
    TEST_ASSERT_EQUAL_UINT8(0, input_get_in(255));
}

/* ===== Debounce (Rules 16, DEBOUNCE_MS=20) ===== */

void test_debounce_immediate_change_not_accepted(void)
{
    hal_stub_set_pin(IN_0_GPIO_Port, IN_0_Pin, GPIO_PIN_SET);
    input_service_process();

    systick_ms += DEBOUNCE_MS - 5;
    input_service_process();

    TEST_ASSERT_EQUAL_UINT8(0, input_get_in(0));
}

void test_debounce_accepted_after_20ms(void)
{
    hal_stub_set_pin(IN_0_GPIO_Port, IN_0_Pin, GPIO_PIN_SET);
    input_service_process();

    systick_ms += DEBOUNCE_MS;
    input_service_process();

    TEST_ASSERT_EQUAL_UINT8(1, input_get_in(0));
}

void test_debounce_bounce_resets_timer(void)
{
    hal_stub_set_pin(IN_0_GPIO_Port, IN_0_Pin, GPIO_PIN_SET);
    input_service_process();

    systick_ms += 15;
    hal_stub_set_pin(IN_0_GPIO_Port, IN_0_Pin, GPIO_PIN_RESET);
    input_service_process();

    systick_ms += 15;
    input_service_process();

    TEST_ASSERT_EQUAL_UINT8(0, input_get_in(0));
}

void test_debounce_multiple_channels_independent(void)
{
    hal_stub_set_pin(IN_0_GPIO_Port, IN_0_Pin, GPIO_PIN_SET);
    input_service_process();

    systick_ms += 5;
    hal_stub_set_pin(IN_5_GPIO_Port, IN_5_Pin, GPIO_PIN_SET);
    input_service_process();

    systick_ms += DEBOUNCE_MS - 5;
    input_service_process();

    TEST_ASSERT_EQUAL_UINT8(1, input_get_in(0));
    TEST_ASSERT_EQUAL_UINT8(0, input_get_in(5));

    systick_ms += 10;
    input_service_process();
    TEST_ASSERT_EQUAL_UINT8(1, input_get_in(5));
}

/* ===== Init ===== */

void test_init_reads_current_pin_state(void)
{
    set_all_pins(GPIO_PIN_SET);
    input_service_init();

    TEST_ASSERT_EQUAL_HEX8(0xFF, input_get_packed());
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_packed_all_zeros);
    RUN_TEST(test_packed_all_ones);
    RUN_TEST(test_packed_pgood_is_bit6);
    RUN_TEST(test_packed_faultz_is_bit7);
    RUN_TEST(test_packed_in0_is_bit0);
    RUN_TEST(test_packed_in5_is_bit5);
    RUN_TEST(test_packed_mixed_pattern);
    RUN_TEST(test_get_pgood_returns_filtered);
    RUN_TEST(test_get_sus_s3_returns_filtered);
    RUN_TEST(test_get_faultz_returns_filtered);
    RUN_TEST(test_get_in_bounds_check);
    RUN_TEST(test_debounce_immediate_change_not_accepted);
    RUN_TEST(test_debounce_accepted_after_20ms);
    RUN_TEST(test_debounce_bounce_resets_timer);
    RUN_TEST(test_debounce_multiple_channels_independent);
    RUN_TEST(test_init_reads_current_pin_state);
    return UNITY_END();
}
