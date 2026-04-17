/*
 * Unit tests: audio state machine (ASEQ) — Rules_POWER.md §9.
 *
 * ON order:  POWER_AUDIO=1 → +10 ms → SDZ=1 → +10 ms → MUTE=0
 * OFF order: MUTE=1 → +10 ms → SDZ=0 → POWER_AUDIO=0
 * Emergency: power_force_off_domains(DOM_AUDIO) drives amp to safe state
 *            (MUTE=1, SDZ=0, POWER_AUDIO=0) without any delay.
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

/* ===== ASEQ ON (Rules §9.1) ===== */

void test_aseq_on_full_order_and_timing(void)
{
    uint8_t r = power_ctrl_request(DOM_AUDIO, DOM_AUDIO);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_ON_POWER, aseq);

    /* Tick 1: POWER_AUDIO driven HIGH, still in ON_WAIT_SDZ */
    tick_ms(1);
    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_TRUE(power_state & DOM_AUDIO);
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(SDZ_GPIO_Port, SDZ_Pin));

    /* After < 10 ms SDZ must still be untouched */
    tick_ms(8);
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(SDZ_GPIO_Port, SDZ_Pin));

    /* Past 10 ms SDZ is driven HIGH */
    tick_ms(3);
    TEST_ASSERT_TRUE(pth_last_gpio_write(SDZ_GPIO_Port, SDZ_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(MUTE_GPIO_Port, MUTE_Pin));

    /* Past another 10 ms MUTE is released (LOW) */
    tick_ms(12);
    TEST_ASSERT_TRUE(pth_last_gpio_write(MUTE_GPIO_Port, MUTE_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);

    tick_ms(2);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);

    /* Ordering: POWER_AUDIO → SDZ → MUTE */
    int i_pwr = pth_first_write_idx(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin);
    int i_sdz = pth_first_write_idx(SDZ_GPIO_Port,  SDZ_Pin);
    int i_mut = pth_first_write_idx(MUTE_GPIO_Port, MUTE_Pin);
    TEST_ASSERT_TRUE(i_pwr >= 0);
    TEST_ASSERT_TRUE(i_sdz > i_pwr);
    TEST_ASSERT_TRUE(i_mut > i_sdz);
}

/* ===== ASEQ OFF (Rules §9.2) ===== */

void test_aseq_off_full_order_and_timing(void)
{
    power_state = DOM_AUDIO;

    uint8_t r = power_ctrl_request(DOM_AUDIO, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_OFF_MUTE, aseq);

    /* Tick 1: MUTE driven HIGH first */
    tick_ms(1);
    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(MUTE_GPIO_Port, MUTE_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(SDZ_GPIO_Port, SDZ_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin));

    /* After < 10 ms SDZ still untouched */
    tick_ms(8);
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(SDZ_GPIO_Port, SDZ_Pin));

    /* Past 10 ms SDZ LOW + POWER_AUDIO LOW happen in consecutive states */
    tick_ms(5);
    TEST_ASSERT_TRUE(pth_last_gpio_write(SDZ_GPIO_Port, SDZ_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);

    TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_AUDIO);
    tick_ms(2);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);

    /* Ordering: MUTE → SDZ → POWER_AUDIO */
    int i_mut = pth_first_write_idx(MUTE_GPIO_Port, MUTE_Pin);
    int i_sdz = pth_first_write_idx(SDZ_GPIO_Port,  SDZ_Pin);
    int i_pwr = pth_first_write_idx(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin);
    TEST_ASSERT_TRUE(i_mut >= 0);
    TEST_ASSERT_TRUE(i_sdz > i_mut);
    TEST_ASSERT_TRUE(i_pwr > i_sdz);
}

/* ===== ASEQ ON from auto-startup safe-on (Rules §6.5 + §9) ===== */

void test_aseq_on_from_safe_on_runs_partial_sdz_mute_only(void)
{
    /* Simulate power_auto_startup post-state per Rules §6.5:
     * DOM_AUDIO already set (POWER_AUDIO=1), amp still safe (SDZ=0, MUTE=1,
     * amp_active=0). POWER_CTRL AUDIO=ON from Q7 must run only the SDZ->MUTE
     * tail of ASEQ without re-driving POWER_AUDIO. */
    power_state = DOM_AUDIO;
    amp_active  = 0;

    uint8_t r = power_ctrl_request(DOM_AUDIO, DOM_AUDIO);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_ON_SDZ, aseq);

    tick_ms(1);
    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(SDZ_GPIO_Port, SDZ_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(MUTE_GPIO_Port, MUTE_Pin));

    tick_ms(12);
    TEST_ASSERT_TRUE(pth_last_gpio_write(MUTE_GPIO_Port, MUTE_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);

    tick_ms(2);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);
    TEST_ASSERT_EQUAL_UINT8(1, amp_active);
    TEST_ASSERT_TRUE(power_state & DOM_AUDIO);
    /* POWER_AUDIO pin was never touched during the partial sequence. */
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin));
}

void test_aseq_on_while_amp_active_is_noop(void)
{
    /* Already in full-on: POWER_CTRL AUDIO=ON must not re-run the sequence. */
    power_state = DOM_AUDIO;
    amp_active  = 1;

    uint8_t r = power_ctrl_request(DOM_AUDIO, DOM_AUDIO);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);

    tick_ms(50);
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(SDZ_GPIO_Port,  SDZ_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(MUTE_GPIO_Port, MUTE_Pin));
}

/* ===== Emergency force-off audio (Rules §6.2, §9.3) ===== */

void test_power_force_off_audio_sets_safe_state_immediately(void)
{
    power_state = DOM_AUDIO;
    aseq = ASEQ_ON_WAIT_SDZ;

    power_force_off_domains(DOM_AUDIO);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(MUTE_GPIO_Port, MUTE_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(SDZ_GPIO_Port, SDZ_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);

    TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_AUDIO);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_aseq_on_full_order_and_timing);
    RUN_TEST(test_aseq_off_full_order_and_timing);
    RUN_TEST(test_aseq_on_from_safe_on_runs_partial_sdz_mute_only);
    RUN_TEST(test_aseq_on_while_amp_active_is_noop);
    RUN_TEST(test_power_force_off_audio_sets_safe_state_immediately);
    return UNITY_END();
}
