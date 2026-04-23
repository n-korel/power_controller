/*
 * Unit tests: SUS_S3# auto-start PWRBTN# pulse — Rules_POWER.md §8.
 *
 *   - SUS_S3# LOW for < SUS_S3_THRESHOLD_MS (500 ms): no pulse
 *   - SUS_S3# LOW for ≥ 500 ms: PWRBTN# active-low pulse of PWRBTN_PULSE_MS
 *   - Bounce to HIGH resets the tracking timer
 *   - After a pulse a SUS_S3_COOLDOWN_MS window blocks the next trigger
 *   - Without PGOOD the SM is inhibited
 *
 * mock_sus_s3 mirrors the raw pin value: 1 = HIGH (inactive), 0 = LOW (active).
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

/* ===== Threshold ===== */

void test_sus_low_below_threshold_does_not_fire_pwrbtn(void)
{
    mock_sus_s3 = 0;

    tick_ms(SUS_S3_THRESHOLD_MS - 1);

    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));
}

void test_sus_low_exact_threshold_fires_pwrbtn(void)
{
    mock_sus_s3 = 0;

    /* tick_ms() increments systick_ms before calling power_manager_process(),
     * so the first LOW sample sets sus_low_since to the first incremented ms.
     * The condition (now - sus_low_since) >= THRESH therefore becomes true
     * after THRESH+1 iterations in this harness. */
    tick_ms(SUS_S3_THRESHOLD_MS + 1);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(PWRBTN_GPIO_Port, PWRBTN_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_EQUAL_UINT32(1, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));
}

void test_sus_low_above_threshold_fires_pwrbtn_pulse_150ms(void)
{
    mock_sus_s3 = 0;

    /* Hit threshold + 5 ms margin */
    tick_ms(SUS_S3_THRESHOLD_MS + 5);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(PWRBTN_GPIO_Port, PWRBTN_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_EQUAL_UINT32(1, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));

    /* Before PWRBTN_PULSE_MS elapses the pin must stay LOW */
    tick_ms(PWRBTN_PULSE_MS - 5);
    TEST_ASSERT_EQUAL_UINT32(1, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));

    /* Past PWRBTN_PULSE_MS the pin is released HIGH */
    tick_ms(10);
    TEST_ASSERT_TRUE(pth_last_gpio_write(PWRBTN_GPIO_Port, PWRBTN_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_EQUAL_UINT32(2, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));
}

/* ===== Debounce ===== */

void test_sus_bounce_resets_timer(void)
{
    /* Hold LOW for 450 ms, bounce HIGH for 1 ms, back LOW for 450 ms.
     * Neither continuous-low span reaches 500 ms → never fires. */
    mock_sus_s3 = 0;
    tick_ms(450);
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));

    mock_sus_s3 = 1;
    tick_ms(1);

    mock_sus_s3 = 0;
    tick_ms(450);

    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));
}

/* ===== Cooldown ===== */

void test_cooldown_5s_blocks_second_trigger(void)
{
    mock_sus_s3 = 0;

    /* First trigger: threshold + full pulse (releases PWRBTN HIGH, starts cooldown) */
    tick_ms(SUS_S3_THRESHOLD_MS + PWRBTN_PULSE_MS + 10);
    uint32_t writes_after_first = pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin);
    TEST_ASSERT_EQUAL_UINT32(2, writes_after_first);

    /* Force a fresh tracking cycle while cooldown is active */
    mock_sus_s3 = 1;
    tick_ms(5);
    mock_sus_s3 = 0;
    tick_ms(SUS_S3_COOLDOWN_MS - 500);

    TEST_ASSERT_EQUAL_UINT32(writes_after_first,
                             pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));
}

void test_second_trigger_after_cooldown_is_allowed(void)
{
    mock_sus_s3 = 0;

    /* First trigger (includes full pulse, ends HIGH and starts cooldown) */
    tick_ms(SUS_S3_THRESHOLD_MS + PWRBTN_PULSE_MS + 10);
    TEST_ASSERT_EQUAL_UINT32(2, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));

    /* Wait out cooldown; keep inputs neutral */
    mock_sus_s3 = 1;
    tick_ms(SUS_S3_COOLDOWN_MS + 1);

    /* Second trigger */
    mock_sus_s3 = 0;
    tick_ms(SUS_S3_THRESHOLD_MS + 1);
    TEST_ASSERT_EQUAL_UINT32(3, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));

    tick_ms(PWRBTN_PULSE_MS + 1);
    TEST_ASSERT_EQUAL_UINT32(4, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));
}

/* ===== PGOOD gating ===== */

void test_sus_ignored_without_pgood(void)
{
    mock_pgood  = 0;
    mock_sus_s3 = 0;

    tick_ms(2 * SUS_S3_THRESHOLD_MS);

    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sus_low_below_threshold_does_not_fire_pwrbtn);
    RUN_TEST(test_sus_low_exact_threshold_fires_pwrbtn);
    RUN_TEST(test_sus_low_above_threshold_fires_pwrbtn_pulse_150ms);
    RUN_TEST(test_sus_bounce_resets_timer);
    RUN_TEST(test_cooldown_5s_blocks_second_trigger);
    RUN_TEST(test_second_trigger_after_cooldown_is_allowed);
    RUN_TEST(test_sus_ignored_without_pgood);
    return UNITY_END();
}
