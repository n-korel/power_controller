/*
 * Unit tests: Backlight brightness (SET_BRIGHTNESS handler) — Rules_POWER.md §13.
 *
 *   - power_set_brightness(pwm): pwm is clamped at 1000 (0..100%, 0.1% step)
 *   - brightness is always stored in brightness_pwm
 *   - CCR1 is written ONLY if DOM_BACKLIGHT is currently in power_state
 *     (Rules: do not drive PWM without the backlight domain being on)
 */
#include "unity.h"
#include "config.h"
#include "power_test_helpers.h"
#include "main.h"
#include "tim.h"

#include "power_manager.c"

void setUp(void)
{
    pth_reset();
    power_manager_init();
    htim17.Instance_data.CCR1 = 0;
}

void tearDown(void) {}

/* ===== Clamping ===== */

void test_brightness_clamped_at_1000(void)
{
    power_state = DOM_BACKLIGHT;

    power_set_brightness(1500);
    TEST_ASSERT_EQUAL_UINT16(1000, brightness_pwm);
    TEST_ASSERT_EQUAL_UINT32(1000, htim17.Instance_data.CCR1);

    power_set_brightness(65535);
    TEST_ASSERT_EQUAL_UINT16(1000, brightness_pwm);
    TEST_ASSERT_EQUAL_UINT32(1000, htim17.Instance_data.CCR1);

    /* Boundary: 1000 is valid, passes through unchanged */
    power_set_brightness(1000);
    TEST_ASSERT_EQUAL_UINT16(1000, brightness_pwm);
    TEST_ASSERT_EQUAL_UINT32(1000, htim17.Instance_data.CCR1);
}

/* ===== Gated by DOM_BACKLIGHT ===== */

void test_brightness_not_applied_to_pwm_when_backlight_off(void)
{
    power_state = DOM_SCALER | DOM_LCD; /* backlight bit absent */
    htim17.Instance_data.CCR1 = 999; /* marker: must stay untouched */

    power_set_brightness(500);

    TEST_ASSERT_EQUAL_UINT16(500, brightness_pwm);
    TEST_ASSERT_EQUAL_UINT32(999, htim17.Instance_data.CCR1);
}

void test_brightness_applied_to_pwm_when_backlight_on(void)
{
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT;

    power_set_brightness(750);
    TEST_ASSERT_EQUAL_UINT16(750, brightness_pwm);
    TEST_ASSERT_EQUAL_UINT32(750, htim17.Instance_data.CCR1);

    power_set_brightness(0);
    TEST_ASSERT_EQUAL_UINT16(0, brightness_pwm);
    TEST_ASSERT_EQUAL_UINT32(0, htim17.Instance_data.CCR1);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_brightness_clamped_at_1000);
    RUN_TEST(test_brightness_not_applied_to_pwm_when_backlight_off);
    RUN_TEST(test_brightness_applied_to_pwm_when_backlight_on);
    return UNITY_END();
}
