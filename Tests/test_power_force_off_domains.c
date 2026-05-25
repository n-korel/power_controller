/*
 * Unit tests: power_force_off_domains() — direct GPIO / state coverage.
 *
 * Called from fault_manager::apply_fault_policy(); previously exercised only
 * indirectly via fault policy tests. Here each domain mask is driven directly.
 */
#include "unity.h"
#include "config.h"
#include "power_test_helpers.h"
#include "main.h"
#include "tim.h"

#include "power_manager.c"

#define DOM_ALL (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | DOM_AUDIO | \
                 DOM_ETH1 | DOM_ETH2 | DOM_TOUCH)

static void seed_all_domains_on(void)
{
    power_state    = DOM_ALL;
    brightness_pwm = 500;
    dseq           = DSEQ_UP_VERIFY_LCD;
    aseq           = ASEQ_ON_WAIT_SDZ;
    amp_active     = 1;
    htim17.Instance_data.CCR1 = 500;
}

static void assert_last_low(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(port, pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
}

static void assert_last_high(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(port, pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
}

static void assert_untouched(GPIO_TypeDef *port, uint16_t pin)
{
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(port, pin));
}

void setUp(void)
{
    pth_reset();
    power_manager_init();
}

void tearDown(void) {}

/* ===== Per-domain: correct GPIO, unrelated pins untouched ===== */

void test_force_off_audio_only(void)
{
    seed_all_domains_on();
    hal_stub_reset();

    power_force_off_domains(DOM_AUDIO);

    assert_last_high(MUTE_GPIO_Port, MUTE_Pin);
    assert_last_low(SDZ_GPIO_Port, SDZ_Pin);
    assert_last_low(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin);

    assert_untouched(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin);
    assert_untouched(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin);
    assert_untouched(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin);
    assert_untouched(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin);
    assert_untouched(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin);
    assert_untouched(POWER_ETH2_GPIO_Port, POWER_ETH2_Pin);
    assert_untouched(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin);

    TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_AUDIO);
    TEST_ASSERT_EQUAL_HEX8(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT |
                           DOM_ETH1 | DOM_ETH2 | DOM_TOUCH, power_state);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);
    TEST_ASSERT_EQUAL_UINT8(0, amp_active);
}

void test_force_off_eth1_only(void)
{
    seed_all_domains_on();
    hal_stub_reset();

    power_force_off_domains(DOM_ETH1);

    assert_last_low(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin);

    assert_untouched(MUTE_GPIO_Port, MUTE_Pin);
    assert_untouched(SDZ_GPIO_Port, SDZ_Pin);
    assert_untouched(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin);
    assert_untouched(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin);
    assert_untouched(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin);
    assert_untouched(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin);
    assert_untouched(POWER_ETH2_GPIO_Port, POWER_ETH2_Pin);
    assert_untouched(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin);

    TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_ETH1);
    TEST_ASSERT_EQUAL_HEX8(DOM_ALL & ~DOM_ETH1, power_state);
}

void test_force_off_eth2_only(void)
{
    seed_all_domains_on();
    hal_stub_reset();

    power_force_off_domains(DOM_ETH2);

    assert_last_low(POWER_ETH2_GPIO_Port, POWER_ETH2_Pin);
    assert_untouched(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin);
    assert_untouched(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin);
    assert_untouched(MUTE_GPIO_Port, MUTE_Pin);
    assert_untouched(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin);

    TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_ETH2);
    TEST_ASSERT_EQUAL_HEX8(DOM_ALL & ~DOM_ETH2, power_state);
}

void test_force_off_touch_only(void)
{
    seed_all_domains_on();
    hal_stub_reset();

    power_force_off_domains(DOM_TOUCH);

    assert_last_low(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin);
    assert_untouched(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin);
    assert_untouched(POWER_ETH2_GPIO_Port, POWER_ETH2_Pin);
    assert_untouched(MUTE_GPIO_Port, MUTE_Pin);
    assert_untouched(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin);

    TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_TOUCH);
    TEST_ASSERT_EQUAL_HEX8(DOM_ALL & ~DOM_TOUCH, power_state);
}

void test_force_off_scaler_uses_emergency_display_path(void)
{
    seed_all_domains_on();
    hal_stub_reset();

    power_force_off_domains(DOM_SCALER);

    assert_last_low(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin);
    assert_last_low(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin);
    assert_last_low(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin);
    assert_last_low(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin);
    TEST_ASSERT_EQUAL_UINT32(0, htim17.Instance_data.CCR1);

    assert_untouched(MUTE_GPIO_Port, MUTE_Pin);
    assert_untouched(SDZ_GPIO_Port, SDZ_Pin);
    assert_untouched(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin);
    assert_untouched(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin);
    assert_untouched(POWER_ETH2_GPIO_Port, POWER_ETH2_Pin);
    assert_untouched(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin);

    TEST_ASSERT_EQUAL_HEX8(0, power_state & (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT));
    TEST_ASSERT_EQUAL_HEX8(DOM_AUDIO | DOM_ETH1 | DOM_ETH2 | DOM_TOUCH, power_state);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
}

void test_force_off_lcd_uses_emergency_display_path(void)
{
    seed_all_domains_on();
    hal_stub_reset();

    power_force_off_domains(DOM_LCD);

    assert_last_low(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin);
    assert_last_low(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin);
    assert_untouched(MUTE_GPIO_Port, MUTE_Pin);
    assert_untouched(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin);

    TEST_ASSERT_EQUAL_HEX8(0, power_state & (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT));
    TEST_ASSERT_EQUAL_HEX8(DOM_AUDIO | DOM_ETH1 | DOM_ETH2 | DOM_TOUCH, power_state);
}

void test_force_off_backlight_uses_emergency_display_path(void)
{
    seed_all_domains_on();
    hal_stub_reset();

    power_force_off_domains(DOM_BACKLIGHT);

    assert_last_low(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin);
    assert_untouched(MUTE_GPIO_Port, MUTE_Pin);
    assert_untouched(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin);

    TEST_ASSERT_EQUAL_HEX8(0, power_state & (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT));
    TEST_ASSERT_EQUAL_HEX8(DOM_AUDIO | DOM_ETH1 | DOM_ETH2 | DOM_TOUCH, power_state);
}

/* ===== ALL domains at once ===== */

void test_force_off_all_domains(void)
{
    seed_all_domains_on();
    hal_stub_reset();

    power_force_off_domains(DOM_ALL);

    assert_last_low(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin);
    assert_last_low(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin);
    assert_last_low(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin);
    assert_last_low(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin);
    assert_last_high(MUTE_GPIO_Port, MUTE_Pin);
    assert_last_low(SDZ_GPIO_Port, SDZ_Pin);
    assert_last_low(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin);
    assert_last_low(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin);
    assert_last_low(POWER_ETH2_GPIO_Port, POWER_ETH2_Pin);
    assert_last_low(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin);

    TEST_ASSERT_EQUAL_UINT8(0, power_state);
    TEST_ASSERT_EQUAL_UINT32(0, htim17.Instance_data.CCR1);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);
    TEST_ASSERT_EQUAL_UINT8(0, amp_active);
}

/* ===== Already-off domains (idempotent) ===== */

void test_force_off_all_when_already_off(void)
{
    power_state = 0;
    dseq = DSEQ_IDLE;
    aseq = ASEQ_IDLE;
    amp_active = 0;
    hal_stub_reset();

    power_force_off_domains(DOM_ALL);

    TEST_ASSERT_EQUAL_UINT8(0, power_state);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);

    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(POWER_ETH2_GPIO_Port, POWER_ETH2_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin));
}

void test_force_off_eth1_when_already_off(void)
{
    power_state = DOM_ETH2 | DOM_TOUCH;
    hal_stub_reset();

    power_force_off_domains(DOM_ETH1);

    TEST_ASSERT_EQUAL_HEX8(DOM_ETH2 | DOM_TOUCH, power_state);
    assert_last_low(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin);
    assert_untouched(POWER_ETH2_GPIO_Port, POWER_ETH2_Pin);
    assert_untouched(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin);
}

void test_force_off_audio_when_already_off(void)
{
    power_state = DOM_ETH1;
    aseq = ASEQ_IDLE;
    amp_active = 0;
    hal_stub_reset();

    power_force_off_domains(DOM_AUDIO);

    TEST_ASSERT_EQUAL_HEX8(DOM_ETH1, power_state);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);
    assert_last_high(MUTE_GPIO_Port, MUTE_Pin);
    assert_last_low(SDZ_GPIO_Port, SDZ_Pin);
    assert_untouched(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_force_off_audio_only);
    RUN_TEST(test_force_off_eth1_only);
    RUN_TEST(test_force_off_eth2_only);
    RUN_TEST(test_force_off_touch_only);
    RUN_TEST(test_force_off_scaler_uses_emergency_display_path);
    RUN_TEST(test_force_off_lcd_uses_emergency_display_path);
    RUN_TEST(test_force_off_backlight_uses_emergency_display_path);
    RUN_TEST(test_force_off_all_domains);
    RUN_TEST(test_force_off_all_when_already_off);
    RUN_TEST(test_force_off_eth1_when_already_off);
    RUN_TEST(test_force_off_audio_when_already_off);
    return UNITY_END();
}
