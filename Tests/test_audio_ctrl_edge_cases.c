/*
 * Unit tests: power_ctrl_request audio edge cases (Rules §6.5, §9).
 *
 * Covers:
 *   - AUDIO=OFF interrupts an in-flight ASEQ ON sequence (AUDIO=OFF has priority)
 *   - Repeated AUDIO=ON while amp_active=1 is a no-op
 *   - AUDIO=ON rejected (status=1) while aseq != ASEQ_IDLE
 *   - AUDIO|TOUCH: TOUCH applied even when AUDIO=ON deferred (ASEQ busy)
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

void test_audio_off_interrupts_in_flight_aseq_on(void)
{
    /* Start full ON sequence and advance into the wait-for-SDZ state. */
    uint8_t r = power_ctrl_request(DOM_AUDIO, DOM_AUDIO);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_ON_POWER, aseq);

    tick_ms(1);
    TEST_ASSERT_EQUAL_INT(ASEQ_ON_WAIT_SDZ, aseq);

    /* AUDIO=OFF must preempt the startup sequence and begin shutdown. */
    r = power_ctrl_request(DOM_AUDIO, 0);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_OFF_MUTE, aseq);

    tick_ms(1);
    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(MUTE_GPIO_Port, MUTE_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
}

void test_audio_on_while_amp_active_is_noop(void)
{
    power_state = DOM_AUDIO;
    amp_active  = 1;
    aseq        = ASEQ_IDLE;

    uint8_t r = power_ctrl_request(DOM_AUDIO, DOM_AUDIO);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_IDLE, aseq);
    TEST_ASSERT_EQUAL_UINT8(1, amp_active);

    tick_ms(50);
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(SDZ_GPIO_Port, SDZ_Pin));
    TEST_ASSERT_EQUAL_UINT32(0, pth_gpio_write_count(MUTE_GPIO_Port, MUTE_Pin));
}

void test_audio_on_rejected_while_aseq_busy(void)
{
    aseq        = ASEQ_ON_WAIT_MUTE;
    power_state = DOM_AUDIO;
    amp_active  = 0;

    uint8_t r = power_ctrl_request(DOM_AUDIO, DOM_AUDIO);
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_ON_WAIT_MUTE, aseq);
}

void test_audio_touch_combined_touch_applied_while_aseq_busy(void)
{
    aseq        = ASEQ_ON_WAIT_SDZ;
    power_state = DOM_AUDIO;
    amp_active  = 0;

    uint8_t r = power_ctrl_request(DOM_AUDIO | DOM_TOUCH, DOM_AUDIO | DOM_TOUCH);
    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_INT(ASEQ_ON_WAIT_SDZ, aseq);
    TEST_ASSERT_TRUE(power_state & DOM_TOUCH);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(pth_last_gpio_write(POWER_TOUCH_GPIO_Port, POWER_TOUCH_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_audio_off_interrupts_in_flight_aseq_on);
    RUN_TEST(test_audio_on_while_amp_active_is_noop);
    RUN_TEST(test_audio_on_rejected_while_aseq_busy);
    RUN_TEST(test_audio_touch_combined_touch_applied_while_aseq_busy);
    return UNITY_END();
}
