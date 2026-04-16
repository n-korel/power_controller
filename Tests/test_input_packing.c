/*
 * Unit tests: Input service — debounce and bit packing (Rules 4.5, 16)
 *
 * input_get_packed() layout (Rules 4.5 GET_STATUS):
 *   bit0..5 = IN0..IN5
 *   bit6    = PGOOD
 *   bit7    = Faultz
 *
 * Debounce: 20ms (DEBOUNCE_MS) — new state accepted only after stable for ≥20ms.
 *
 * Source included directly for access to static state (filtered[], raw_prev[]).
 */
#include "unity.h"
#include "config.h"

volatile uint32_t systick_ms;

#include "input_service.c"

/* Helper: set all mock GPIO pins to a given state */
static void set_all_pins(GPIO_PinState val)
{
    for (uint8_t i = 0; i < INPUT_COUNT; i++)
        hal_stub_set_pin(pins[i].port, pins[i].pin, val);
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
    /* All inputs LOW → packed = 0x00 */
    TEST_IGNORE_MESSAGE("TODO: all filtered[]=0, assert input_get_packed()==0x00");
}

void test_packed_all_ones(void)
{
    /* All inputs HIGH → packed = 0xFF (bits 0-7 all set) */
    TEST_IGNORE_MESSAGE("TODO: set all pins HIGH, debounce, assert input_get_packed()==0xFF");
}

void test_packed_pgood_is_bit6(void)
{
    /* Only PGOOD=1 → packed = 0x40 */
    TEST_IGNORE_MESSAGE("TODO: set only PGOOD HIGH, debounce, assert input_get_packed()==0x40");
}

void test_packed_faultz_is_bit7(void)
{
    /* Only FAULTZ=1 → packed = 0x80 */
    TEST_IGNORE_MESSAGE("TODO: set only FAULTZ HIGH, debounce, assert input_get_packed()==0x80");
}

void test_packed_in0_is_bit0(void)
{
    /* Only IN_0=1 → packed = 0x01 */
    TEST_IGNORE_MESSAGE("TODO: set only IN_0 HIGH, debounce, assert input_get_packed()==0x01");
}

void test_packed_in5_is_bit5(void)
{
    /* Only IN_5=1 → packed = 0x20 */
    TEST_IGNORE_MESSAGE("TODO: set only IN_5 HIGH, debounce, assert input_get_packed()==0x20");
}

void test_packed_mixed_pattern(void)
{
    /* PGOOD=1, IN_0=1, IN_3=1 → packed = 0x49 */
    TEST_IGNORE_MESSAGE("TODO: set PGOOD+IN_0+IN_3 HIGH, debounce, assert 0x49");
}

/* ===== Individual getters ===== */

void test_get_pgood_returns_filtered(void)
{
    TEST_IGNORE_MESSAGE("TODO: set PGOOD pin HIGH, debounce, assert input_get_pgood()==1");
}

void test_get_sus_s3_returns_filtered(void)
{
    TEST_IGNORE_MESSAGE("TODO: set SUS_S3 pin HIGH, debounce, assert input_get_sus_s3()==1");
}

void test_get_faultz_returns_filtered(void)
{
    TEST_IGNORE_MESSAGE("TODO: set FAULTZ pin HIGH, debounce, assert input_get_faultz()==1");
}

void test_get_in_bounds_check(void)
{
    /* input_get_in(6) → 0 (out of range) */
    TEST_IGNORE_MESSAGE("TODO: assert input_get_in(6)==0 and input_get_in(255)==0");
}

/* ===== Debounce (Rules 16, DEBOUNCE_MS=20) ===== */

void test_debounce_immediate_change_not_accepted(void)
{
    /* Pin changes, but < 20ms passes → filtered stays old value */
    TEST_IGNORE_MESSAGE("TODO: set pin HIGH, advance <20ms, process, assert filtered still LOW");
}

void test_debounce_accepted_after_20ms(void)
{
    /* Pin changes and stays stable for ≥ 20ms → filtered updates */
    TEST_IGNORE_MESSAGE("TODO: set pin HIGH, advance 20ms, process, assert filtered == HIGH");
}

void test_debounce_bounce_resets_timer(void)
{
    /* Pin toggles within 20ms → timer restarts, no change accepted */
    TEST_IGNORE_MESSAGE("TODO: set HIGH, advance 15ms, set LOW, advance 15ms, assert filtered unchanged");
}

void test_debounce_multiple_channels_independent(void)
{
    /* Channel 0 debounces independently from channel 5 */
    TEST_IGNORE_MESSAGE("TODO: change ch0 and ch5 at different times, verify independent debounce");
}

/* ===== Init ===== */

void test_init_reads_current_pin_state(void)
{
    /* After init, filtered[] reflects current GPIO state (no debounce delay) */
    TEST_IGNORE_MESSAGE("TODO: set pins before init, call init, assert filtered matches GPIO state");
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    /* Packing */
    RUN_TEST(test_packed_all_zeros);
    RUN_TEST(test_packed_all_ones);
    RUN_TEST(test_packed_pgood_is_bit6);
    RUN_TEST(test_packed_faultz_is_bit7);
    RUN_TEST(test_packed_in0_is_bit0);
    RUN_TEST(test_packed_in5_is_bit5);
    RUN_TEST(test_packed_mixed_pattern);
    /* Getters */
    RUN_TEST(test_get_pgood_returns_filtered);
    RUN_TEST(test_get_sus_s3_returns_filtered);
    RUN_TEST(test_get_faultz_returns_filtered);
    RUN_TEST(test_get_in_bounds_check);
    /* Debounce */
    RUN_TEST(test_debounce_immediate_change_not_accepted);
    RUN_TEST(test_debounce_accepted_after_20ms);
    RUN_TEST(test_debounce_bounce_resets_timer);
    RUN_TEST(test_debounce_multiple_channels_independent);
    /* Init */
    RUN_TEST(test_init_reads_current_pin_state);
    return UNITY_END();
}
