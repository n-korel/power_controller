/*
 * Unit tests: power_effective_state_for_request() masking during dseq.
 *
 * DOWN: effective state must not expose SCALER/LCD/BACKLIGHT even if
 *       power_state still holds stale ON bits — BACKLIGHT=ON must fail Rule §23.
 * UP:   effective state ORs in pending SCALER+LCD; BACKLIGHT follows
 *       dseq_up_with_bl. Rule §23 may pass while dseq!=IDLE still rejects the cmd.
 */
#include "unity.h"
#include "config.h"
#include "power_test_helpers.h"

#include "power_manager.c"

#define DOM_DISPLAY (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT)
#define ALWAYS_ON_ETH  (DOM_ETH1 | DOM_ETH2)

static const dseq_state_t k_down_states[] = {
    DSEQ_DN_PWM_ZERO,
    DSEQ_DN_WAIT_PWM,
    DSEQ_DN_BL_OFF,
    DSEQ_DN_WAIT_BL,
    DSEQ_DN_LCD_OFF,
    DSEQ_DN_WAIT_LCD,
    DSEQ_DN_RST_ASSERT,
    DSEQ_DN_SCALER_OFF,
    DSEQ_DN_DONE,
};

static const dseq_state_t k_up_states[] = {
    DSEQ_UP_SCALER_ON,
    DSEQ_UP_WAIT_SCALER,
    DSEQ_UP_VERIFY_SCALER,
    DSEQ_UP_RST_RELEASE,
    DSEQ_UP_WAIT_RST,
    DSEQ_UP_LCD_ON,
    DSEQ_UP_WAIT_LCD,
    DSEQ_UP_VERIFY_LCD,
    DSEQ_UP_BL_ON,
    DSEQ_UP_VERIFY_BL,
    DSEQ_UP_DONE,
};

static const dseq_state_t k_bloff_states[] = {
    DSEQ_BLOFF_PWM_ZERO,
    DSEQ_BLOFF_WAIT,
    DSEQ_BLOFF_GPIO,
    DSEQ_BLOFF_DONE,
};

/* Mirrors Rules §23 guard in power_ctrl_request(). */
static uint8_t rule23_rejects_backlight_on(uint8_t req_state, uint16_t mask, uint16_t value)
{
    uint8_t future = (uint8_t)((req_state & ~(uint8_t)mask) | (uint8_t)(value & mask));
    if ((mask & DOM_BACKLIGHT) && (value & DOM_BACKLIGHT) &&
        (!(future & DOM_SCALER) || !(future & DOM_LCD))) {
        return 1;
    }
    return 0;
}

static void seed_stale_display_on(void)
{
    power_state = DOM_DISPLAY;
    brightness_pwm = 500;
}

void setUp(void)
{
    pth_reset();
    power_manager_init();
}

void tearDown(void) {}

/* ===== Direct effective-state bitmask tests ===== */

void test_effective_down_states_strip_display_even_if_power_state_on(void)
{
    seed_stale_display_on();

    for (size_t i = 0; i < sizeof(k_down_states) / sizeof(k_down_states[0]); i++) {
        dseq = k_down_states[i];
        uint8_t eff = power_effective_state_for_request();
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, eff & DOM_DISPLAY,
                                     "DOWN dseq must hide display domains");
    }
}

void test_effective_up_states_expose_pending_scaler_lcd(void)
{
    power_state = 0;
    dseq_up_with_bl = 0;

    for (size_t i = 0; i < sizeof(k_up_states) / sizeof(k_up_states[0]); i++) {
        dseq = k_up_states[i];
        uint8_t eff = power_effective_state_for_request();
        TEST_ASSERT_TRUE_MESSAGE(eff & DOM_SCALER, "UP dseq must expose pending SCALER");
        TEST_ASSERT_TRUE_MESSAGE(eff & DOM_LCD, "UP dseq must expose pending LCD");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, eff & DOM_BACKLIGHT,
                                     "UP without dseq_up_with_bl hides BACKLIGHT");
    }
}

void test_effective_up_states_include_backlight_when_dseq_up_with_bl(void)
{
    power_state = 0;
    dseq_up_with_bl = 1;

    for (size_t i = 0; i < sizeof(k_up_states) / sizeof(k_up_states[0]); i++) {
        dseq = k_up_states[i];
        uint8_t eff = power_effective_state_for_request();
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(DOM_DISPLAY, eff & DOM_DISPLAY,
                                       "UP with dseq_up_with_bl exposes full display");
    }
}

void test_effective_bloff_states_strip_backlight_only(void)
{
    seed_stale_display_on();

    for (size_t i = 0; i < sizeof(k_bloff_states) / sizeof(k_bloff_states[0]); i++) {
        dseq = k_bloff_states[i];
        uint8_t eff = power_effective_state_for_request();
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(DOM_SCALER | DOM_LCD, eff & (DOM_SCALER | DOM_LCD),
                                       "BLOFF keeps SCALER/LCD in effective state");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, eff & DOM_BACKLIGHT,
                                       "BLOFF hides BACKLIGHT");
    }
}

void test_effective_idle_reflects_power_state(void)
{
    power_state = DOM_SCALER | DOM_LCD;
    dseq = DSEQ_IDLE;
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(DOM_SCALER | DOM_LCD | ALWAYS_ON_ETH),
                           power_effective_state_for_request());
}

/* ===== Rule §23 via effective state (BACKLIGHT=ON) ===== */

void test_rule23_rejects_backlight_on_during_all_down_states(void)
{
    seed_stale_display_on();

    for (size_t i = 0; i < sizeof(k_down_states) / sizeof(k_down_states[0]); i++) {
        dseq = k_down_states[i];
        uint8_t eff = power_effective_state_for_request();
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1,
                                        rule23_rejects_backlight_on(eff, DOM_BACKLIGHT, DOM_BACKLIGHT),
                                        "Rule §23 must reject BL=ON while DOWN effective");
    }
}

void test_rule23_accepts_backlight_on_during_up_without_bl_flag(void)
{
    power_state = 0;
    dseq_up_with_bl = 0;

    for (size_t i = 0; i < sizeof(k_up_states) / sizeof(k_up_states[0]); i++) {
        dseq = k_up_states[i];
        uint8_t eff = power_effective_state_for_request();
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0,
                                        rule23_rejects_backlight_on(eff, DOM_BACKLIGHT, DOM_BACKLIGHT),
                                        "Rule §23 must allow BL=ON while UP effective (SCALER+LCD pending)");
    }
}

void test_rule23_accepts_backlight_on_during_up_with_bl_flag(void)
{
    power_state = 0;
    dseq_up_with_bl = 1;

    for (size_t i = 0; i < sizeof(k_up_states) / sizeof(k_up_states[0]); i++) {
        dseq = k_up_states[i];
        uint8_t eff = power_effective_state_for_request();
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0,
                                        rule23_rejects_backlight_on(eff, DOM_BACKLIGHT, DOM_BACKLIGHT),
                                        "Rule §23 must allow idempotent BL=ON when UP plans BL");
    }
}

/* ===== power_ctrl_request integration ===== */

void test_backlight_on_rejected_during_down_sequence(void)
{
    seed_stale_display_on();

    for (size_t i = 0; i < sizeof(k_down_states) / sizeof(k_down_states[0]); i++) {
        dseq = k_down_states[i];
        const dseq_state_t before = dseq;
        const uint8_t ps_before = power_state;

        uint8_t r = power_ctrl_request(DOM_BACKLIGHT, DOM_BACKLIGHT);

        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, r, "BACKLIGHT=ON during DOWN must be rejected");
        TEST_ASSERT_EQUAL_INT_MESSAGE(before, dseq, "dseq must not change");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE((uint8_t)(ps_before | ALWAYS_ON_ETH), power_state,
                                       "power_state must not change except always-on ETH");
    }
}

void test_backlight_on_rejected_during_up_sequence_sequencer_busy(void)
{
    power_state = 0;
    dseq_up_with_bl = 0;

    for (size_t i = 0; i < sizeof(k_up_states) / sizeof(k_up_states[0]); i++) {
        dseq = k_up_states[i];
        const dseq_state_t before = dseq;

        uint8_t r = power_ctrl_request(DOM_BACKLIGHT, DOM_BACKLIGHT);

        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, r, "BACKLIGHT=ON during UP must be rejected (busy)");
        TEST_ASSERT_EQUAL_INT_MESSAGE(before, dseq, "dseq must not change");
    }
}

void test_backlight_on_accepted_at_idle_when_scaler_lcd_on(void)
{
    power_state = DOM_SCALER | DOM_LCD;
    dseq = DSEQ_IDLE;
    hal_stub_reset();

    uint8_t r = power_ctrl_request(DOM_BACKLIGHT, DOM_BACKLIGHT);

    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(DSEQ_UP_BL_ON, dseq);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_effective_down_states_strip_display_even_if_power_state_on);
    RUN_TEST(test_effective_up_states_expose_pending_scaler_lcd);
    RUN_TEST(test_effective_up_states_include_backlight_when_dseq_up_with_bl);
    RUN_TEST(test_effective_bloff_states_strip_backlight_only);
    RUN_TEST(test_effective_idle_reflects_power_state);
    RUN_TEST(test_rule23_rejects_backlight_on_during_all_down_states);
    RUN_TEST(test_rule23_accepts_backlight_on_during_up_without_bl_flag);
    RUN_TEST(test_rule23_accepts_backlight_on_during_up_with_bl_flag);
    RUN_TEST(test_backlight_on_rejected_during_down_sequence);
    RUN_TEST(test_backlight_on_rejected_during_up_sequence_sequencer_busy);
    RUN_TEST(test_backlight_on_accepted_at_idle_when_scaler_lcd_on);
    return UNITY_END();
}
