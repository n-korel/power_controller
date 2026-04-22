/*
 * Unit tests: non-blocking startup state machine (STARTUP_*).
 *
 * Covers Rules_POWER.md:
 *   - §1.1  no blocking waits outside main loop
 *   - §6.5  PGOOD=HIGH -> auto-bring up SCALER+LCD (no BL), TOUCH, AUDIO (safe)
 *   - §6.5  PGOOD absent for 5 s -> latch FAULT_PGOOD_LOST, stay safe
 *   - §12   safe state preserved on timeout, HAL_IWDG_Refresh stays in main loop
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
    htim17.Instance_data.CCR1 = 0;
}

void tearDown(void) {}

/* ===== Initial state (Rules §3.2, §12) ===== */

void test_init_leaves_startup_sm_idle(void)
{
    TEST_ASSERT_EQUAL_INT(STARTUP_IDLE, sseq);
    TEST_ASSERT_EQUAL_UINT32(0, sseq_timer);
}

void test_process_without_begin_is_noop(void)
{
    /* Even if PGOOD is HIGH and a lot of time passes, without explicit
     * power_startup_begin() nothing must happen — the auto-startup must be
     * gated by main() calling power_startup_begin() exactly once. */
    mock_pgood = 1;
    tick_ms(PGOOD_TIMEOUT_MS + 1000);

    TEST_ASSERT_EQUAL_INT(STARTUP_IDLE, sseq);
    TEST_ASSERT_EQUAL_UINT8(0, power_state);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT32(0, fault_flags_set);
}

/* ===== power_startup_begin() arms the SM (Rules §6.5) ===== */

void test_startup_begin_arms_wait_and_captures_timer(void)
{
    systick_ms = 12345;

    power_startup_begin();

    TEST_ASSERT_EQUAL_INT(STARTUP_WAIT_PGOOD, sseq);
    TEST_ASSERT_EQUAL_UINT32(12345, sseq_timer);
}

void test_startup_begin_is_blocked_while_fault_is_latched(void)
{
    fault_flags_set = FAULT_PGOOD_LOST;
    sseq = STARTUP_IDLE;
    sseq_timer = 0;

    power_startup_begin();

    TEST_ASSERT_EQUAL_INT(STARTUP_IDLE, sseq);
    TEST_ASSERT_EQUAL_UINT32(0, sseq_timer);
}

/* ===== PGOOD=HIGH -> auto-startup (Rules §6.5) ===== */

void test_pgood_high_triggers_auto_startup_and_returns_idle(void)
{
    mock_pgood = 1;
    power_startup_begin();

    /* One main-loop tick is enough for sseq_process() to see PGOOD=HIGH,
     * call power_auto_startup() and hand control off to dseq. */
    tick_ms(1);

    TEST_ASSERT_EQUAL_INT(STARTUP_IDLE, sseq);
    /* Auto-startup per Rules §6.5 / README §13.6: SCALER UP sequence
     * without BL, TOUCH ON, AUDIO ON (POWER_AUDIO pin HIGH, DOM_AUDIO bit
     * set) while the amplifier stays safe (SDZ=0, MUTE=1 from init). */
    TEST_ASSERT_EQUAL_UINT8(0, dseq_up_with_bl);
    TEST_ASSERT_TRUE(power_state & DOM_TOUCH);
    TEST_ASSERT_TRUE(power_state & DOM_AUDIO);
    TEST_ASSERT_EQUAL_UINT32(0, fault_flags_set);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_TRUE(pth_last_gpio_write(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    /* Amp must remain in safe state: SDZ/MUTE untouched, amp_active=0 */
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(SDZ_GPIO_Port,  SDZ_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(MUTE_GPIO_Port, MUTE_Pin));
    TEST_ASSERT_EQUAL_UINT8(0, amp_active);
}

void test_pgood_high_full_up_completes_without_backlight(void)
{
    mock_pgood = 1;
    mock_raw_avg[ADC_IDX_SCALER_POWER] = 1500;
    mock_raw_avg[ADC_IDX_LCD_POWER]    = 1500;

    power_startup_begin();
    tick_ms(300);

    TEST_ASSERT_EQUAL_INT(STARTUP_IDLE, sseq);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    /* Per Rules §6.5 / README §13.6: SCALER=ON, LCD=ON, BACKLIGHT=OFF,
     * TOUCH=ON, AUDIO=ON (amp safe). */
    TEST_ASSERT_EQUAL_HEX8(DOM_SCALER | DOM_LCD | DOM_TOUCH | DOM_AUDIO, power_state);
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, fault_flags_set);
}

/* ===== PGOOD=LOW within timeout window stays waiting (Rules §6.5) ===== */

void test_pgood_low_below_timeout_stays_waiting(void)
{
    mock_pgood = 0;
    power_startup_begin();

    tick_ms(PGOOD_TIMEOUT_MS - 1);

    TEST_ASSERT_EQUAL_INT(STARTUP_WAIT_PGOOD, sseq);
    TEST_ASSERT_EQUAL_UINT32(0, fault_flags_set);
    TEST_ASSERT_EQUAL_UINT8(0, power_state);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
}

void test_pgood_high_on_last_ms_still_triggers_auto_startup(void)
{
    /* Keep PGOOD low up to the last millisecond before timeout, then flip
     * to HIGH: sseq_process() must see PGOOD=HIGH before the timeout check
     * and run auto-startup (no fault must be latched). */
    mock_pgood = 0;
    power_startup_begin();
    tick_ms(PGOOD_TIMEOUT_MS - 2);
    TEST_ASSERT_EQUAL_INT(STARTUP_WAIT_PGOOD, sseq);

    mock_pgood = 1;
    tick_ms(1);

    TEST_ASSERT_EQUAL_INT(STARTUP_IDLE, sseq);
    TEST_ASSERT_TRUE(power_state & DOM_TOUCH);
    TEST_ASSERT_EQUAL_UINT32(0, fault_flags_set);
}

/* ===== PGOOD=LOW past timeout -> FAULT_PGOOD_LOST (Rules §6.5, §7.1) ===== */

void test_pgood_absent_5s_latches_fault_and_stays_safe(void)
{
    mock_pgood = 0;
    power_startup_begin();

    tick_ms(PGOOD_TIMEOUT_MS);

    TEST_ASSERT_EQUAL_INT(STARTUP_IDLE, sseq);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_PGOOD_LOST);
    /* Domains must remain fully OFF (safe state) */
    TEST_ASSERT_EQUAL_UINT8(0, power_state);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    /* No domain GPIO was ever driven HIGH (fault policy may write LOW to
     * enforce safe state, which is fine). */
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(LCD_POWER_ON_GPIO_Port,    LCD_POWER_ON_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(POWER_TOUCH_GPIO_Port,     POWER_TOUCH_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(POWER_AUDIO_GPIO_Port,     POWER_AUDIO_Pin));
}

void test_pgood_recovery_after_timeout_does_not_auto_startup(void)
{
    /* After the timeout latched FAULT_PGOOD_LOST, merely raising PGOOD must
     * not resurrect the auto-startup — that would violate the "latched fault,
     * no auto-recovery" rule (§7.1). Only an explicit power_startup_begin()
     * may re-arm the wait. */
    mock_pgood = 0;
    power_startup_begin();
    tick_ms(PGOOD_TIMEOUT_MS);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_PGOOD_LOST);

    mock_pgood = 1;
    tick_ms(10);

    TEST_ASSERT_EQUAL_INT(STARTUP_IDLE, sseq);
    TEST_ASSERT_EQUAL_UINT8(0, power_state);
    TEST_ASSERT_EQUAL_INT(DSEQ_IDLE, dseq);
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_high_count(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin));
}

/* ===== Timer math uses unsigned wraparound (§12 systick_ms invariant) ===== */

void test_timeout_works_across_systick_wrap(void)
{
    /* Arm the wait just before the uint32_t wrap point, then advance past it.
     * Timeout is computed as (now - sseq_timer) >= PGOOD_TIMEOUT_MS, which
     * must stay correct through wraparound. */
    systick_ms = 0xFFFFFFFFU - 100U;
    mock_pgood = 0;
    power_startup_begin();

    tick_ms(PGOOD_TIMEOUT_MS);

    TEST_ASSERT_EQUAL_INT(STARTUP_IDLE, sseq);
    TEST_ASSERT_TRUE(fault_flags_set & FAULT_PGOOD_LOST);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_leaves_startup_sm_idle);
    RUN_TEST(test_process_without_begin_is_noop);
    RUN_TEST(test_startup_begin_arms_wait_and_captures_timer);
    RUN_TEST(test_startup_begin_is_blocked_while_fault_is_latched);
    RUN_TEST(test_pgood_high_triggers_auto_startup_and_returns_idle);
    RUN_TEST(test_pgood_high_full_up_completes_without_backlight);
    RUN_TEST(test_pgood_low_below_timeout_stays_waiting);
    RUN_TEST(test_pgood_high_on_last_ms_still_triggers_auto_startup);
    RUN_TEST(test_pgood_absent_5s_latches_fault_and_stays_safe);
    RUN_TEST(test_pgood_recovery_after_timeout_does_not_auto_startup);
    RUN_TEST(test_timeout_works_across_systick_wrap);
    return UNITY_END();
}
