/*
 * Unit tests: power_safe_state() + power_emergency_display_off()
 *
 * Covers the safety-critical invariants of Rules_POWER.md:
 *   - §3.2  safe state on boot / after any fault
 *   - §6.2  emergency display off (no delays) on PGOOD/SEQ fault
 *   - §12   immutable invariants (all domains OFF, SDZ=0, MUTE=1,
 *           RST_CH7511B=0, PWM=0, OD pins released)
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
}

void tearDown(void) {}

/* ===== power_safe_state() — Rules §3.2, §12 ===== */

void test_safe_state_all_domains_off(void)
{
    /* Seed with every domain marked as "on" so we can observe the reset */
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | DOM_AUDIO |
                  DOM_ETH1 | DOM_ETH2 | DOM_TOUCH;

    power_safe_state();

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(POWER_ETH2_GPIO_Port, POWER_ETH2_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);

    TEST_ASSERT_EQUAL_UINT8(0, power_state);
}

void test_safe_state_amp_sdz_low_mute_high(void)
{
    power_safe_state();

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(SDZ_GPIO_Port, SDZ_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(MUTE_GPIO_Port, MUTE_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
}

void test_safe_state_pwm_zero(void)
{
    /* Seed CCR1 with a non-zero value so we can detect it being cleared */
    htim17.Instance_data.CCR1 = 750;

    power_safe_state();

    TEST_ASSERT_EQUAL_UINT32(0, htim17.Instance_data.CCR1);
}

void test_safe_state_rst_ch7511b_low(void)
{
    power_safe_state();

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
}

void test_safe_state_od_pins_released(void)
{
    power_safe_state();

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(PWRBTN_GPIO_Port, PWRBTN_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(RSTBTN_GPIO_Port, RSTBTN_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
}

void test_safe_state_resets_sequencers(void)
{
    dseq = DSEQ_UP_WAIT_SCALER;
    aseq = ASEQ_ON_WAIT_SDZ;

    power_safe_state();

    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);
}

/* ===== power_emergency_display_off() — Rules §6.2 ===== */

void test_emergency_display_off_sets_all_display_off(void)
{
    htim17.Instance_data.CCR1 = 500;
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | DOM_AUDIO | DOM_TOUCH;

    power_emergency_display_off();

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);

    TEST_ASSERT_EQUAL_UINT32(0, htim17.Instance_data.CCR1);
}

void test_emergency_off_clears_only_display_bits(void)
{
    /* AUDIO, ETH1/2, TOUCH must survive an emergency display shutdown */
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | DOM_AUDIO |
                  DOM_ETH1 | DOM_ETH2 | DOM_TOUCH;

    power_emergency_display_off();

    TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_SCALER);
    TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_LCD);
    TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_BACKLIGHT);
    TEST_ASSERT_EQUAL_HEX8(DOM_AUDIO, power_state & DOM_AUDIO);
    TEST_ASSERT_EQUAL_HEX8(DOM_ETH1,  power_state & DOM_ETH1);
    TEST_ASSERT_EQUAL_HEX8(DOM_ETH2,  power_state & DOM_ETH2);
    TEST_ASSERT_EQUAL_HEX8(DOM_TOUCH, power_state & DOM_TOUCH);
}

void test_emergency_off_does_not_touch_amp_or_od(void)
{
    /* Emergency display off must not modify amplifier or OD button pins
     * (Rules §6.2: display-only rail collapse). */
    power_emergency_display_off();

    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(SDZ_GPIO_Port,  SDZ_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(MUTE_GPIO_Port, MUTE_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(PWRBTN_GPIO_Port, PWRBTN_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(RSTBTN_GPIO_Port, RSTBTN_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin));
}

void test_emergency_off_resets_dseq_to_idle(void)
{
    dseq = DSEQ_UP_VERIFY_LCD;

    power_emergency_display_off();

    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_safe_state_all_domains_off);
    RUN_TEST(test_safe_state_amp_sdz_low_mute_high);
    RUN_TEST(test_safe_state_pwm_zero);
    RUN_TEST(test_safe_state_rst_ch7511b_low);
    RUN_TEST(test_safe_state_od_pins_released);
    RUN_TEST(test_safe_state_resets_sequencers);
    RUN_TEST(test_emergency_display_off_sets_all_display_off);
    RUN_TEST(test_emergency_off_clears_only_display_bits);
    RUN_TEST(test_emergency_off_does_not_touch_amp_or_od);
    RUN_TEST(test_emergency_off_resets_dseq_to_idle);
    return UNITY_END();
}
