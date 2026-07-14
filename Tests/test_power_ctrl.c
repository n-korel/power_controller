/*
 * Unit tests: power_ctrl_request validation (Rules 4.5, 6.1-6.6, 9)
 *
 * Checks:
 *   - BACKLIGHT ON requires SCALER + LCD ON
 *   - LCD ON requires SCALER ON
 *   - Sequencer-busy rejection
 *   - Simple domains (ETH/TOUCH) direct control
 *   - Audio → audio SM startup
 *   - SCALER/LCD OFF → full shutdown sequence (incl. §24 with BACKLIGHT ON)
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

#define ALWAYS_ON_ETH  (DOM_ETH1 | DOM_ETH2)

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
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(DOM_LCD | ALWAYS_ON_ETH), power_state);
}

void test_backlight_on_rejected_without_lcd(void)
{
    power_state = DOM_SCALER;

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, DOM_BACKLIGHT);

    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(DOM_SCALER | ALWAYS_ON_ETH), power_state);
}

void test_backlight_on_rejected_without_both(void)
{
    power_state = 0;

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, DOM_BACKLIGHT);

    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_UINT8(ALWAYS_ON_ETH, power_state);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
}

void test_backlight_on_accepted_with_scaler_and_lcd(void)
{
    power_state = DOM_SCALER | DOM_LCD;

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, DOM_BACKLIGHT);

#if (ENABLE_BACKLIGHT_HW == 0U)
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
#else
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_BL_ON, dseq);
#endif
}

void test_conflicting_scaler_off_and_backlight_on_is_rejected_atomically(void)
{
    /* Conflict in one request: SCALER=OFF and BACKLIGHT=ON.
     * Must be rejected without starting any sequencing or changing state. */
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | ALWAYS_ON_ETH;
    dseq = DSEQ_IDLE;

    uint16_t mask  = DOM_SCALER | DOM_BACKLIGHT;
    uint16_t value = DOM_BACKLIGHT; /* scaler off */

    uint8_t r = power_ctrl_request(mask, value);
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | ALWAYS_ON_ETH), power_state);
    TEST_ASSERT_EQUAL_UINT32(0, hal_gpio_log_count);
}

void test_idempotent_request_does_not_restart_display_sequence_or_toggle_gpio(void)
{
    /* Already ON: repeating the same request must be a no-op. */
    power_state = DOM_SCALER | DOM_LCD | ALWAYS_ON_ETH;
    dseq = DSEQ_IDLE;
    hal_stub_reset();

    uint8_t r = power_ctrl_request(DOM_SCALER | DOM_LCD, DOM_SCALER | DOM_LCD);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(DOM_SCALER | DOM_LCD | ALWAYS_ON_ETH), power_state);
    TEST_ASSERT_EQUAL_UINT32(0, hal_gpio_log_count);
}

void test_unknown_bits_in_mask_or_value_are_rejected(void)
{
    power_state = 0;
    hal_stub_reset();

    uint16_t mask  = 0x0080;
    uint16_t value = 0x0080;

    uint8_t r = power_ctrl_request(mask, value);
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_UINT8(ALWAYS_ON_ETH, power_state);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT32(0, hal_gpio_log_count);
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

    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_ON_POWER, aseq);
}

/* ===== Simple domains (ETH1, ETH2, TOUCH) ===== */

void test_eth1_request_is_ignored_but_state_stays_on(void)
{
    power_state = DOM_ETH2;

    uint8_t r = power_ctrl_request(DOM_ETH1, DOM_ETH1);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_TRUE(power_state & DOM_ETH1);
    TEST_ASSERT_EQUAL_UINT32(0, hal_gpio_log_count);
}

void test_eth2_off_request_is_ignored_and_state_stays_on(void)
{
    power_state = DOM_ETH1;

    uint8_t r = power_ctrl_request(DOM_ETH2, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_HEX8(DOM_ETH2, power_state & DOM_ETH2);
    TEST_ASSERT_EQUAL_UINT32(0, hal_gpio_log_count);
}

void test_touch_on_direct(void)
{
    power_state = 0;

    uint8_t r = power_ctrl_request(DOM_TOUCH, DOM_TOUCH);
#if (ENABLE_TOUCH_HW == 0U)
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_UINT8(ALWAYS_ON_ETH, power_state);
    TEST_ASSERT_EQUAL_UINT32(0, hal_gpio_log_count);
#else
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_TRUE(power_state & DOM_TOUCH);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(last_gpio_write(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
#endif
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

/* ===== PGOOD interlock (Rules_POWER.md invariant 43) ===== */

void test_display_up_rejected_when_pgood_low_scaler_on(void)
{
    power_state = 0;
    dseq = DSEQ_IDLE;
    hal_stub_reset();
    mock_pgood = 0;

    uint8_t r = power_ctrl_request(DOM_SCALER, DOM_SCALER);

    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8(ALWAYS_ON_ETH, power_state);
    TEST_ASSERT_EQUAL_UINT32(0, hal_gpio_log_count);
}

void test_display_up_rejected_when_pgood_low_lcd_on_with_scaler_already_on(void)
{
    power_state = DOM_SCALER;
    dseq = DSEQ_IDLE;
    hal_stub_reset();
    mock_pgood = 0;

    uint8_t r = power_ctrl_request(DOM_LCD, DOM_LCD);

    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(DOM_SCALER | ALWAYS_ON_ETH), power_state);
    TEST_ASSERT_EQUAL_UINT32(0, hal_gpio_log_count);
}

void test_display_up_rejected_when_pgood_low_backlight_on_with_scaler_lcd_on(void)
{
    power_state = DOM_SCALER | DOM_LCD;
    dseq = DSEQ_IDLE;
    hal_stub_reset();
    mock_pgood = 0;

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, DOM_BACKLIGHT);

    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(DOM_SCALER | DOM_LCD | ALWAYS_ON_ETH), power_state);
    TEST_ASSERT_EQUAL_UINT32(0, hal_gpio_log_count);
}

void test_scaler_off_starts_full_down_sequence(void)
{
    power_state = DOM_SCALER | DOM_LCD;

    uint8_t r = power_ctrl_request(DOM_SCALER, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_DN_PWM_ZERO, dseq);
}

void test_scaler_off_with_backlight_on_starts_full_down_sequence(void)
{
    /* Rules §24: SCALER/LCD OFF while BACKLIGHT is ON must not be rejected;
     * must start full DN sequencing (not confused with §23 BL-ON guard). */
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT;
    dseq = DSEQ_IDLE;

    uint8_t r = power_ctrl_request(DOM_SCALER, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_DN_PWM_ZERO, dseq);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | ALWAYS_ON_ETH), power_state);
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
#if (ENABLE_AUDIO_HW == 0U)
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);
#else
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_ON_POWER, aseq);
#endif
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
#if (ENABLE_BACKLIGHT_HW == 0U)
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8(ALWAYS_ON_ETH, power_state);
#else
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_SCALER_ON, dseq);
    TEST_ASSERT_EQUAL_UINT8(1, dseq_up_with_bl);
#endif
}

void test_multiple_simple_domains_at_once(void)
{
    power_state = 0;
    uint16_t mask = DOM_ETH1 | DOM_ETH2 | DOM_TOUCH;

    uint8_t r = power_ctrl_request(mask, mask);
#if (ENABLE_TOUCH_HW == 0U)
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_UINT8(ALWAYS_ON_ETH, power_state);
    TEST_ASSERT_EQUAL_UINT32(0, hal_gpio_log_count);
#else
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_TRUE(power_state & DOM_ETH1);
    TEST_ASSERT_TRUE(power_state & DOM_ETH2);
    TEST_ASSERT_TRUE(power_state & DOM_TOUCH);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(last_gpio_write(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
#endif
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_backlight_on_rejected_without_scaler);
    RUN_TEST(test_backlight_on_rejected_without_lcd);
    RUN_TEST(test_backlight_on_rejected_without_both);
    RUN_TEST(test_backlight_on_accepted_with_scaler_and_lcd);
    RUN_TEST(test_conflicting_scaler_off_and_backlight_on_is_rejected_atomically);
    RUN_TEST(test_idempotent_request_does_not_restart_display_sequence_or_toggle_gpio);
    RUN_TEST(test_unknown_bits_in_mask_or_value_are_rejected);
    RUN_TEST(test_lcd_on_rejected_without_scaler);
    RUN_TEST(test_lcd_on_accepted_with_scaler);
    RUN_TEST(test_display_cmd_rejected_when_sequencer_busy);
    RUN_TEST(test_audio_cmd_rejected_when_aseq_busy);
    RUN_TEST(test_eth1_request_is_ignored_but_state_stays_on);
    RUN_TEST(test_eth2_off_request_is_ignored_and_state_stays_on);
    RUN_TEST(test_touch_on_direct);
    RUN_TEST(test_scaler_on_starts_full_up_sequence);
    RUN_TEST(test_display_up_rejected_when_pgood_low_scaler_on);
    RUN_TEST(test_display_up_rejected_when_pgood_low_lcd_on_with_scaler_already_on);
    RUN_TEST(test_display_up_rejected_when_pgood_low_backlight_on_with_scaler_lcd_on);
    RUN_TEST(test_scaler_off_starts_full_down_sequence);
    RUN_TEST(test_scaler_off_with_backlight_on_starts_full_down_sequence);
    RUN_TEST(test_bl_off_starts_bl_only_shutdown);
    RUN_TEST(test_audio_on_starts_audio_sequence);
    RUN_TEST(test_audio_off_starts_mute_first);
    RUN_TEST(test_scaler_lcd_bl_on_together);
    RUN_TEST(test_multiple_simple_domains_at_once);
    return UNITY_END();
}
