/*
 * Unit tests: power_ctrl_request validation (Rules 4.5, 6.1-6.6, 9)
 *
 * Checks:
 *   - BACKLIGHT ON requires SCALER + LCD ON
 *   - LCD ON requires SCALER ON
 *   - Sequencer-busy rejection
 *   - Simple domains (ETH/TOUCH) direct control
 *   - Audio → audio SM startup
 *   - SCALER/LCD OFF → full shutdown sequence
 */
#include "unity.h"
#include "config.h"

static uint16_t mock_raw_avg[14];
static uint8_t  mock_pgood = 1;
static uint8_t  mock_sus_s3 = 1;

uint16_t adc_get_raw_avg(uint8_t idx) { return (idx < 14) ? mock_raw_avg[idx] : 0; }
uint8_t  input_get_pgood(void)  { return mock_pgood; }
uint8_t  input_get_sus_s3(void) { return mock_sus_s3; }
void     fault_set_flag(uint16_t flag) { (void)flag; }

volatile uint32_t systick_ms;

#include "power_manager.c"

/* Find the last GPIO write record targeting a specific pin */
static int last_gpio_write(const GPIO_TypeDef *port, uint16_t pin, GPIO_PinState *out)
{
    for (int32_t i = (int32_t)hal_gpio_log_count - 1; i >= 0; i--) {
        if (hal_gpio_log[i].port == port && hal_gpio_log[i].pin == pin) {
            *out = hal_gpio_log[i].state;
            return 1;
        }
    }
    return 0;
}

void setUp(void)
{
    power_manager_init();
    hal_stub_reset();
    mock_pgood = 1;
    systick_ms = 1000;
}

void tearDown(void) {}

/* ===== BACKLIGHT constraints (Rules 4.5, 6.6) ===== */

void test_backlight_on_rejected_without_scaler(void)
{
    power_state = DOM_LCD;

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, DOM_BACKLIGHT);

    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
}

void test_backlight_on_rejected_without_lcd(void)
{
    power_state = DOM_SCALER;

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, DOM_BACKLIGHT);

    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
}

void test_backlight_on_rejected_without_both(void)
{
    power_state = 0;

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, DOM_BACKLIGHT);

    TEST_ASSERT_EQUAL_UINT8(1, r);
}

void test_backlight_on_accepted_with_scaler_and_lcd(void)
{
    power_state = DOM_SCALER | DOM_LCD;

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, DOM_BACKLIGHT);

    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_BL_ON, dseq);
}

/* ===== LCD constraints ===== */

void test_lcd_on_rejected_without_scaler(void)
{
    power_state = 0;

    uint8_t r = power_ctrl_request(DOM_LCD, DOM_LCD);

    TEST_ASSERT_EQUAL_UINT8(1, r);
}

void test_lcd_on_accepted_with_scaler(void)
{
    power_state = DOM_SCALER;

    uint8_t r = power_ctrl_request(DOM_LCD, DOM_LCD);

    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_RST_RELEASE, dseq);
}

/* ===== Sequencer busy ===== */

void test_display_cmd_rejected_when_sequencer_busy(void)
{
    dseq = DSEQ_UP_WAIT_SCALER;
    power_state = 0;

    uint8_t r = power_ctrl_request(DOM_SCALER, DOM_SCALER);

    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_WAIT_SCALER, dseq);
}

void test_audio_cmd_rejected_when_aseq_busy(void)
{
    aseq = ASEQ_ON_POWER;
    power_state = 0;

    uint8_t r = power_ctrl_request(DOM_AUDIO, DOM_AUDIO);

    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_ON_POWER, aseq);
}

/* ===== Simple domains (ETH1, ETH2, TOUCH) ===== */

void test_eth1_on_direct(void)
{
    power_state = 0;

    uint8_t r = power_ctrl_request(DOM_ETH1, DOM_ETH1);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_TRUE(power_state & DOM_ETH1);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(last_gpio_write(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
}

void test_eth2_off_direct(void)
{
    power_state = DOM_ETH2;

    uint8_t r = power_ctrl_request(DOM_ETH2, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_ETH2);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(last_gpio_write(POWER_ETH2_GPIO_Port, POWER_ETH2_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
}

void test_touch_on_direct(void)
{
    power_state = 0;

    uint8_t r = power_ctrl_request(DOM_TOUCH, DOM_TOUCH);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_TRUE(power_state & DOM_TOUCH);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(last_gpio_write(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
}

/* ===== Full UP/DOWN sequencing trigger ===== */

void test_scaler_on_starts_full_up_sequence(void)
{
    power_state = 0;

    uint8_t r = power_ctrl_request(DOM_SCALER, DOM_SCALER);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_SCALER_ON, dseq);
    TEST_ASSERT_EQUAL_UINT8(0, dseq_up_with_bl);
}

void test_scaler_off_starts_full_down_sequence(void)
{
    power_state = DOM_SCALER | DOM_LCD;

    uint8_t r = power_ctrl_request(DOM_SCALER, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_DN_PWM_ZERO, dseq);
}

void test_bl_off_starts_bl_only_shutdown(void)
{
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT;

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_BLOFF_PWM_ZERO, dseq);
}

/* ===== Audio sequencing ===== */

void test_audio_on_starts_audio_sequence(void)
{
    power_state = 0;

    uint8_t r = power_ctrl_request(DOM_AUDIO, DOM_AUDIO);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_ON_POWER, aseq);
}

void test_audio_off_starts_mute_first(void)
{
    power_state = DOM_AUDIO;

    uint8_t r = power_ctrl_request(DOM_AUDIO, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_OFF_MUTE, aseq);
}

/* ===== Combined requests ===== */

void test_scaler_lcd_bl_on_together(void)
{
    power_state = 0;
    uint16_t mask  = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT;
    uint16_t value = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT;

    uint8_t r = power_ctrl_request(mask, value);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_SCALER_ON, dseq);
    TEST_ASSERT_EQUAL_UINT8(1, dseq_up_with_bl);
}

void test_multiple_simple_domains_at_once(void)
{
    power_state = 0;
    uint16_t mask = DOM_ETH1 | DOM_ETH2 | DOM_TOUCH;

    uint8_t r = power_ctrl_request(mask, mask);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_TRUE(power_state & DOM_ETH1);
    TEST_ASSERT_TRUE(power_state & DOM_ETH2);
    TEST_ASSERT_TRUE(power_state & DOM_TOUCH);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(last_gpio_write(POWER_ETH1_GPIO_Port, POWER_ETH1_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_TRUE(last_gpio_write(POWER_ETH2_GPIO_Port, POWER_ETH2_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_TRUE(last_gpio_write(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_backlight_on_rejected_without_scaler);
    RUN_TEST(test_backlight_on_rejected_without_lcd);
    RUN_TEST(test_backlight_on_rejected_without_both);
    RUN_TEST(test_backlight_on_accepted_with_scaler_and_lcd);
    RUN_TEST(test_lcd_on_rejected_without_scaler);
    RUN_TEST(test_lcd_on_accepted_with_scaler);
    RUN_TEST(test_display_cmd_rejected_when_sequencer_busy);
    RUN_TEST(test_audio_cmd_rejected_when_aseq_busy);
    RUN_TEST(test_eth1_on_direct);
    RUN_TEST(test_eth2_off_direct);
    RUN_TEST(test_touch_on_direct);
    RUN_TEST(test_scaler_on_starts_full_up_sequence);
    RUN_TEST(test_scaler_off_starts_full_down_sequence);
    RUN_TEST(test_bl_off_starts_bl_only_shutdown);
    RUN_TEST(test_audio_on_starts_audio_sequence);
    RUN_TEST(test_audio_off_starts_mute_first);
    RUN_TEST(test_scaler_lcd_bl_on_together);
    RUN_TEST(test_multiple_simple_domains_at_once);
    return UNITY_END();
}
