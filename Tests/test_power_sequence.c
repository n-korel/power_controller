/*
 * Unit tests: display-sequencing state machine (DSEQ).
 *
 * Covers Rules_POWER.md:
 *   - §6   UP sequence + PGOOD abort
 *   - §6.2 emergency display off on SEQ fault
 *   - §13  DOWN / partial / BL-only sub-sequences
 *
 * Ticks power_manager_process() at 1 ms granularity and checks
 * final power_state, GPIO spy log ordering and CCR1 (PWM) value.
 */
#include "unity.h"
#include "config.h"
#include "power_test_helpers.h"
#include "main.h"
#include "tim.h"

#include "power_manager.c"

/* Advance simulated time by `ms` milliseconds, ticking the state
 * machine once per ms (as production main loop does). */
static void tick_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        systick_ms++;
        power_manager_process();
    }
}

/* raw=1500 → adc_mv≈915 → vin_mv ≈ 10629, well above every SEQ_VERIFY_*_MV */
static void seed_valid_adc(void)
{
    mock_raw_avg[ADC_IDX_SCALER_POWER] = 1500;
    mock_raw_avg[ADC_IDX_LCD_POWER]    = 1500;
    mock_raw_avg[ADC_IDX_BL_POWER]     = 1500;
}

void setUp(void)
{
    pth_reset();
    power_manager_init();
    htim17.Instance_data.CCR1 = 0;
}

void tearDown(void) {}

/* ===== UP sequencing (Rules §6, §13) ===== */

void test_full_up_sequence_completes_with_bl(void)
{
    seed_valid_adc();

    uint8_t r = power_ctrl_request(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT,
                                   DOM_SCALER | DOM_LCD | DOM_BACKLIGHT);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_SCALER_ON, dseq);
    TEST_ASSERT_EQUAL_UINT8(1, dseq_up_with_bl);

    tick_ms(300);

    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT, power_state);
    TEST_ASSERT_EQUAL_UINT32(0, fault_flags_set);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);

    /* Check action ordering: SCALER → RST → LCD → BL (Rules §13.7) */
    int i_scaler = pth_first_write_idx(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin);
    int i_rst    = pth_first_write_idx(RST_CH7511B_GPIO_Port,    RST_CH7511B_Pin);
    int i_lcd    = pth_first_write_idx(LCD_POWER_ON_GPIO_Port,   LCD_POWER_ON_Pin);
    int i_bl     = pth_first_write_idx(BACKLIGHT_ON_GPIO_Port,   BACKLIGHT_ON_Pin);
    TEST_ASSERT_TRUE(i_scaler >= 0);
    TEST_ASSERT_TRUE(i_rst > i_scaler);
    TEST_ASSERT_TRUE(i_lcd  > i_rst);
    TEST_ASSERT_TRUE(i_bl   > i_lcd);
}

void test_full_up_without_bl_leaves_backlight_off(void)
{
    seed_valid_adc();

    uint8_t r = power_ctrl_request(DOM_SCALER | DOM_LCD, DOM_SCALER | DOM_LCD);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_UINT8(0, dseq_up_with_bl);

    tick_ms(250);

    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8(DOM_SCALER | DOM_LCD, power_state);
    /* BL pin must never have been driven HIGH */
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin));
}

/* ===== UP verify timeouts (Rules §6) ===== */

void test_up_sequence_scaler_verify_timeout_triggers_seq_abort(void)
{
    /* SCALER ADC stays at 0 — verify always fails, 200 ms timeout → abort */
    mock_raw_avg[ADC_IDX_LCD_POWER] = 1500;
    mock_raw_avg[ADC_IDX_BL_POWER]  = 1500;

    power_ctrl_request(DOM_SCALER, DOM_SCALER);

    tick_ms(SEQ_DELAY_SCALER_ON + SEQ_VERIFY_TIMEOUT + 20);

    TEST_ASSERT_TRUE(fault_flags_set & FAULT_SEQ_ABORT);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_SCALER);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_HEX8(0, power_state & (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT));

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
}

void test_up_sequence_lcd_verify_timeout_triggers_seq_abort(void)
{
    mock_raw_avg[ADC_IDX_SCALER_POWER] = 1500;
    /* LCD ADC low */
    mock_raw_avg[ADC_IDX_BL_POWER]     = 1500;

    power_ctrl_request(DOM_SCALER | DOM_LCD, DOM_SCALER | DOM_LCD);

    tick_ms(SEQ_DELAY_SCALER_ON + SEQ_DELAY_RST_RELEASE +
            SEQ_DELAY_LCD_ON + SEQ_VERIFY_TIMEOUT + 20);

    TEST_ASSERT_TRUE(fault_flags_set & FAULT_SEQ_ABORT);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_LCD);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_HEX8(0, power_state & (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT));
}

void test_up_sequence_bl_verify_timeout_triggers_seq_abort(void)
{
    mock_raw_avg[ADC_IDX_SCALER_POWER] = 1500;
    mock_raw_avg[ADC_IDX_LCD_POWER]    = 1500;
    /* BL ADC stays 0 */

    power_ctrl_request(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT,
                       DOM_SCALER | DOM_LCD | DOM_BACKLIGHT);

    tick_ms(SEQ_DELAY_SCALER_ON + SEQ_DELAY_RST_RELEASE +
            SEQ_DELAY_LCD_ON + SEQ_VERIFY_TIMEOUT + 20);

    TEST_ASSERT_TRUE(fault_flags_set & FAULT_SEQ_ABORT);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_BACKLIGHT);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_HEX8(0, power_state & (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT));
    TEST_ASSERT_EQUAL_UINT32(0, htim17.Instance_data.CCR1);
}

/* ===== PGOOD abort during UP (Rules §6.2) ===== */

void test_pgood_lost_during_up_aborts_with_both_flags(void)
{
    seed_valid_adc();

    power_ctrl_request(DOM_SCALER | DOM_LCD, DOM_SCALER | DOM_LCD);

    /* Advance a bit so we're mid-sequence, then drop PGOOD */
    tick_ms(30);
    TEST_ASSERT_NOT_EQUAL(DSEQ_IDLE, dseq);

    mock_pgood = 0;
    tick_ms(2);

    TEST_ASSERT_TRUE(fault_flags_set & FAULT_PGOOD_LOST);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_SEQ_ABORT);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_HEX8(0, power_state & (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT));

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
}

void test_pgood_lost_during_down_does_not_introduce_unsafe_transitions(void)
{
    /* Contract focus: DOWN sequencing should remain safe and monotonic even if
     * PGOOD drops mid-down (PGOOD check is for UP; fault policy handles PGOOD elsewhere). */
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT;
    htim17.Instance_data.CCR1 = 600;
    mock_pgood = 1;

    uint8_t r = power_ctrl_request(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_DN_PWM_ZERO, dseq);

    tick_ms(5);
    mock_pgood = 0;
    tick_ms(150);

    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8(0, power_state);
    /* No new flags must be synthesized by the DOWN SM itself. */
    TEST_ASSERT_EQUAL_UINT32(0, fault_flags_set & (FAULT_PGOOD_LOST | FAULT_SEQ_ABORT));
}

void test_reentrancy_guard_rejects_new_full_up_while_active(void)
{
    seed_valid_adc();
    power_ctrl_request(DOM_SCALER | DOM_LCD, DOM_SCALER | DOM_LCD);
    TEST_ASSERT_NOT_EQUAL(DSEQ_IDLE, dseq);

    uint8_t r = power_ctrl_request(DOM_SCALER, DOM_SCALER);
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_NOT_EQUAL(DSEQ_IDLE, dseq);
}

void test_reentrancy_guard_rejects_new_full_down_while_active(void)
{
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT;
    power_ctrl_request(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT, 0);
    TEST_ASSERT_NOT_EQUAL(DSEQ_IDLE, dseq);

    uint8_t r = power_ctrl_request(DOM_SCALER, 0);
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_NOT_EQUAL(DSEQ_IDLE, dseq);
}

void test_reentrancy_guard_rejects_new_bl_only_off_while_active(void)
{
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT;
    power_ctrl_request(DOM_BACKLIGHT, 0);
    TEST_ASSERT_NOT_EQUAL(DSEQ_IDLE, dseq);

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, 0);
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_NOT_EQUAL(DSEQ_IDLE, dseq);
}

void test_reentrancy_guard_rejects_new_lcd_partial_up_while_active(void)
{
    power_state = DOM_SCALER;
    seed_valid_adc();
    power_ctrl_request(DOM_LCD, DOM_LCD);
    TEST_ASSERT_NOT_EQUAL(DSEQ_IDLE, dseq);

    uint8_t r = power_ctrl_request(DOM_LCD, DOM_LCD);
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_NOT_EQUAL(DSEQ_IDLE, dseq);
}

void test_verify_timeout_boundaries_scaler_stage(void)
{
    /* SCALER rail stays low → verify fails until timeout boundary. */
    mock_raw_avg[ADC_IDX_LCD_POWER] = 1500;
    mock_raw_avg[ADC_IDX_BL_POWER]  = 1500;
    power_ctrl_request(DOM_SCALER, DOM_SCALER);

    /* Advance into VERIFY stage. */
    tick_ms(SEQ_DELAY_SCALER_ON + 1);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_VERIFY_SCALER, dseq);

    /* timeout-1: no abort yet */
    tick_ms(SEQ_VERIFY_TIMEOUT - 1);
    TEST_ASSERT_EQUAL_UINT32(0, fault_flags_set & FAULT_SEQ_ABORT);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_VERIFY_SCALER, dseq);

    /* ==timeout: abort must latch */
    tick_ms(1);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_SEQ_ABORT);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_SCALER);
}

void test_verify_timeout_boundaries_lcd_stage(void)
{
    mock_raw_avg[ADC_IDX_SCALER_POWER] = 1500;
    mock_raw_avg[ADC_IDX_BL_POWER]     = 1500;
    power_ctrl_request(DOM_SCALER | DOM_LCD, DOM_SCALER | DOM_LCD);

    /* Advance to LCD VERIFY stage (robust to off-by-one tick boundaries). */
    for (uint32_t i = 0; i < 1000 && dseq != DSEQ_UP_VERIFY_LCD; i++)
        tick_ms(1);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_VERIFY_LCD, dseq);

    tick_ms(SEQ_VERIFY_TIMEOUT - 1);
    TEST_ASSERT_EQUAL_UINT32(0, fault_flags_set & FAULT_SEQ_ABORT);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_VERIFY_LCD, dseq);

    tick_ms(1);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_SEQ_ABORT);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_LCD);
}

void test_verify_timeout_boundaries_bl_stage(void)
{
    mock_raw_avg[ADC_IDX_SCALER_POWER] = 1500;
    mock_raw_avg[ADC_IDX_LCD_POWER]    = 1500;
    power_ctrl_request(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT,
                       DOM_SCALER | DOM_LCD | DOM_BACKLIGHT);

    /* Advance to BL VERIFY stage. */
    for (uint32_t i = 0; i < 1000 && dseq != DSEQ_UP_VERIFY_BL; i++)
        tick_ms(1);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_VERIFY_BL, dseq);

    tick_ms(SEQ_VERIFY_TIMEOUT - 1);
    TEST_ASSERT_EQUAL_UINT32(0, fault_flags_set & FAULT_SEQ_ABORT);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_VERIFY_BL, dseq);

    tick_ms(1);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_SEQ_ABORT);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_BACKLIGHT);
}

/* ===== DOWN sequence (Rules §13.7) ===== */

void test_full_down_sequence_orders_pwm_bl_lcd_rst_scaler(void)
{
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT;
    htim17.Instance_data.CCR1 = 600;
    /* Request SCALER OFF -> full shutdown sequence */
    uint8_t r = power_ctrl_request(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_DN_PWM_ZERO, dseq);

    /* First tick must zero PWM before touching any display GPIO */
    systick_ms++;
    power_manager_process();
    TEST_ASSERT_EQUAL_UINT32(0, htim17.Instance_data.CCR1);
    TEST_ASSERT_EQUAL_INT(-1, pth_first_write_idx(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin));

    /* Allow the full DOWN sequence to drain */
    tick_ms(100);

    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8(0, power_state);

    /* Final GPIO states */
    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);

    /* Ordering: BL → LCD → RST → SCALER */
    int i_bl     = pth_first_write_idx(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin);
    int i_lcd    = pth_first_write_idx(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin);
    int i_rst    = pth_first_write_idx(RST_CH7511B_GPIO_Port,  RST_CH7511B_Pin);
    int i_scaler = pth_first_write_idx(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin);
    TEST_ASSERT_TRUE(i_bl >= 0);
    TEST_ASSERT_TRUE(i_lcd    > i_bl);
    TEST_ASSERT_TRUE(i_rst    > i_lcd);
    TEST_ASSERT_TRUE(i_scaler > i_rst);
}

/* ===== BL-only OFF with 10 ms delay (Rules §13.7) ===== */

void test_bloff_only_sequence_10ms_delay_then_gpio(void)
{
    power_state = DOM_SCALER | DOM_LCD | DOM_BACKLIGHT;
    htim17.Instance_data.CCR1 = 700;

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_BLOFF_PWM_ZERO, dseq);

    /* First tick zeroes PWM; BL GPIO must NOT yet have been written */
    tick_ms(1);
    TEST_ASSERT_EQUAL_UINT32(0, htim17.Instance_data.CCR1);
    TEST_ASSERT_EQUAL_INT(-1, pth_first_write_idx(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin));

    /* After < 10 ms the BL GPIO must still be untouched */
    tick_ms(8);
    TEST_ASSERT_EQUAL_INT(-1, pth_first_write_idx(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin));

    /* Past 10 ms the BL GPIO finally drops */
    tick_ms(10);
    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);

    /* SCALER and LCD must stay ON (BL-only off) */
    TEST_ASSERT_EQUAL_HEX8(DOM_SCALER | DOM_LCD, power_state);
    TEST_ASSERT_EQUAL_UINT32(0,
        pth_gpio_write_count(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin));
    TEST_ASSERT_EQUAL_UINT32(0,
        pth_gpio_write_count(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin));

    tick_ms(5);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
}

/* ===== LCD ON when SCALER already ON → partial UP (Rules §13.7) ===== */

void test_lcd_on_with_scaler_already_on_uses_partial_seq(void)
{
    power_state = DOM_SCALER;
    seed_valid_adc();

    uint8_t r = power_ctrl_request(DOM_LCD, DOM_LCD);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_RST_RELEASE, dseq);

    tick_ms(200);

    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT8(DOM_SCALER | DOM_LCD, power_state);
    /* SCALER GPIO must NOT have been re-driven */
    TEST_ASSERT_EQUAL_UINT32(0,
        pth_gpio_write_count(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin));
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_up_sequence_completes_with_bl);
    RUN_TEST(test_full_up_without_bl_leaves_backlight_off);
    RUN_TEST(test_up_sequence_scaler_verify_timeout_triggers_seq_abort);
    RUN_TEST(test_up_sequence_lcd_verify_timeout_triggers_seq_abort);
    RUN_TEST(test_up_sequence_bl_verify_timeout_triggers_seq_abort);
    RUN_TEST(test_pgood_lost_during_up_aborts_with_both_flags);
    RUN_TEST(test_pgood_lost_during_down_does_not_introduce_unsafe_transitions);
    RUN_TEST(test_full_down_sequence_orders_pwm_bl_lcd_rst_scaler);
    RUN_TEST(test_bloff_only_sequence_10ms_delay_then_gpio);
    RUN_TEST(test_lcd_on_with_scaler_already_on_uses_partial_seq);
    RUN_TEST(test_reentrancy_guard_rejects_new_full_up_while_active);
    RUN_TEST(test_reentrancy_guard_rejects_new_full_down_while_active);
    RUN_TEST(test_reentrancy_guard_rejects_new_bl_only_off_while_active);
    RUN_TEST(test_reentrancy_guard_rejects_new_lcd_partial_up_while_active);
    RUN_TEST(test_verify_timeout_boundaries_scaler_stage);
    RUN_TEST(test_verify_timeout_boundaries_lcd_stage);
    RUN_TEST(test_verify_timeout_boundaries_bl_stage);
    return UNITY_END();
}
