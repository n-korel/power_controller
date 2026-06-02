/*
 * Cross-domain fault isolation (Rules_POWER.md invariant #42 + §7.1).
 *
 * Linear tests in test_fault_policy.c assert safe_state per fault flag in
 * isolation. Here we verify the full host recovery path:
 *
 *   1) fault in domain X -> full power_safe_state (all domains OFF);
 *   2) fault_clear_flags() (RESET_FAULT) does not re-enable anything;
 *   3) POWER_CTRL can turn on domain Y independently (Y != X).
 *
 * Cases are driven from a matrix (fault_flag, recover_domain) rather than
 * one linear scenario per fault type.
 */
#include "unity.h"
#include "config.h"
#include "main.h"
#include "tim.h"

static uint16_t mock_voltage_mv[4];
static int16_t  mock_current_ma[5];
static uint16_t mock_raw_avg[14];
static uint8_t  mock_pgood = 1;
static uint8_t  mock_faultz = 1;
static uint8_t  mock_sus_s3 = 1;

uint16_t adc_get_voltage_mv(uint8_t idx) { return (idx < 4) ? mock_voltage_mv[idx] : 0; }
int16_t  adc_get_current_ma(uint8_t idx) { return (idx < 5) ? mock_current_ma[idx] : 0; }
uint16_t adc_get_raw_avg(uint8_t idx) { return (idx < 14) ? mock_raw_avg[idx] : 0; }
uint8_t  adc_service_consume_new_sample(void) { return 1; }
uint8_t  input_get_pgood(void)  { return mock_pgood; }
uint8_t  input_get_faultz(void) { return mock_faultz; }
uint8_t  input_get_sus_s3(void) { return mock_sus_s3; }

volatile uint32_t systick_ms;

#include "power_manager.c"
#include "fault_manager.c"

#define ALL_DOMAINS  (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | DOM_AUDIO | \
                      DOM_ETH1 | DOM_ETH2 | DOM_TOUCH)

#define SCALER_UP_MS  (SEQ_DELAY_SCALER_ON + SEQ_VERIFY_TIMEOUT + 30U)
#define AUDIO_ON_MS   (AUDIO_SDZ_DELAY_MS + AUDIO_MUTE_DELAY_MS + 5U)

typedef struct {
    uint16_t fault_flag;
    uint8_t  seed_state;
    uint16_t recover_mask;
    uint16_t recover_value;
    uint8_t  expect_at_least_on; /* recovery domain(s) that must be ON */
    uint8_t  must_stay_off;      /* domains that must remain OFF (cross-domain) */
    const char *label;
} isolation_case_t;

static void set_v_nominal(void)
{
    mock_voltage_mv[0] = 24000;
    mock_voltage_mv[1] = 12000;
    mock_voltage_mv[2] = 5000;
    mock_voltage_mv[3] = 3300;
}

static void set_i_nominal(void)
{
    for (uint8_t i = 0; i < 5; i++) {
        mock_current_ma[i] = 0;
    }
}

static void seed_valid_display_adc(void)
{
    mock_raw_avg[ADC_IDX_SCALER_POWER] = 1500;
    mock_raw_avg[ADC_IDX_LCD_POWER]    = 1500;
    mock_raw_avg[ADC_IDX_BL_POWER]     = 1500;
}

static void tick_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        systick_ms++;
        power_manager_process();
    }
}

static int last_gpio_write(const GPIO_TypeDef *port, uint16_t pin, GPIO_PinState *out)
{
    for (int32_t i = (int32_t)hal_gpio_log_count - 1; i >= 0; i--) {
        if (hal_gpio_log[i].port == port && hal_gpio_log[i].pin == pin) {
            if (out) {
                *out = hal_gpio_log[i].state;
            }
            return 1;
        }
    }
    return 0;
}

static void assert_full_safe_state(void)
{
    TEST_ASSERT_EQUAL_HEX8(0, power_state);
    GPIO_PinState st;
    TEST_ASSERT_TRUE(last_gpio_write(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(last_gpio_write(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(last_gpio_write(SDZ_GPIO_Port, SDZ_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
    TEST_ASSERT_TRUE(last_gpio_write(MUTE_GPIO_Port, MUTE_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_EQUAL_UINT32(0, htim17.Instance_data.CCR1);
}

static void run_isolation_case(const isolation_case_t *c)
{
    hal_stub_reset();
    power_manager_init();
    fault_manager_init();
    set_v_nominal();
    set_i_nominal();
    seed_valid_display_adc();
    mock_pgood  = 1;
    mock_faultz = 1;
    systick_ms  = 1000;

    power_state = c->seed_state;
    fault_set_flag(c->fault_flag);

    TEST_ASSERT_TRUE_MESSAGE(fault_get_flags() & c->fault_flag, c->label);
    assert_full_safe_state();

    fault_clear_flags();
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, fault_get_flags(), c->label);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, power_state, c->label);

    uint8_t r = power_ctrl_request(c->recover_mask, c->recover_value);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, r, c->label);

    if (c->recover_value & DOM_SCALER) {
        tick_ms(SCALER_UP_MS);
    } else if (c->recover_value & DOM_AUDIO) {
        tick_ms(AUDIO_ON_MS);
    } else {
        tick_ms(5);
    }

    TEST_ASSERT_TRUE_MESSAGE(power_state & c->expect_at_least_on, c->label);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, power_state & c->must_stay_off, c->label);

    GPIO_PinState st;
    if (c->expect_at_least_on & DOM_SCALER) {
        TEST_ASSERT_TRUE(last_gpio_write(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, &st));
        TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    }
    if (c->expect_at_least_on & DOM_AUDIO) {
        TEST_ASSERT_TRUE(last_gpio_write(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin, &st));
        TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
        TEST_ASSERT_TRUE(last_gpio_write(SDZ_GPIO_Port, SDZ_Pin, &st));
        TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    }
    if (c->must_stay_off & DOM_AUDIO) {
        TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_AUDIO);
    }
    if (c->must_stay_off & DOM_SCALER) {
        TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_SCALER);
    }
}

void setUp(void) {}
void tearDown(void) {}

/* ===== Matrix: fault X -> recover Y (not only AUDIO->SCALER) ===== */

static const isolation_case_t k_cases[] = {
    {
        FAULT_AUDIO, DOM_AUDIO | DOM_SCALER,
        DOM_SCALER, DOM_SCALER, DOM_SCALER,
        DOM_AUDIO,
        "FAULT_AUDIO -> SCALER",
    },
#if (ENABLE_AUDIO_HW != 0U)
    {
        FAULT_SCALER, DOM_SCALER | DOM_LCD,
        DOM_AUDIO, DOM_AUDIO, DOM_AUDIO,
        DOM_SCALER | DOM_LCD | DOM_BACKLIGHT,
        "FAULT_SCALER -> AUDIO",
    },
#else
    {
        FAULT_SCALER, DOM_SCALER | DOM_LCD,
        DOM_ETH1, DOM_ETH1, DOM_ETH1,
        DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | DOM_AUDIO,
        "FAULT_SCALER -> ETH1",
    },
#endif
    {
        FAULT_LCD, DOM_SCALER | DOM_LCD | DOM_BACKLIGHT,
        DOM_SCALER, DOM_SCALER, DOM_SCALER,
        DOM_AUDIO | DOM_BACKLIGHT,
        "FAULT_LCD -> SCALER",
    },
#if (ENABLE_AUDIO_HW != 0U)
    {
        FAULT_BACKLIGHT, DOM_SCALER | DOM_LCD | DOM_BACKLIGHT,
        DOM_AUDIO, DOM_AUDIO, DOM_AUDIO,
        DOM_SCALER | DOM_LCD | DOM_BACKLIGHT,
        "FAULT_BACKLIGHT -> AUDIO",
    },
#else
    {
        FAULT_BACKLIGHT, DOM_SCALER | DOM_LCD | DOM_BACKLIGHT,
        DOM_ETH1, DOM_ETH1, DOM_ETH1,
        DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | DOM_AUDIO,
        "FAULT_BACKLIGHT -> ETH1",
    },
#endif
    {
        FAULT_V5_RANGE, DOM_SCALER,
        DOM_SCALER, DOM_SCALER, DOM_SCALER,
        DOM_AUDIO,
        "FAULT_V5_RANGE -> SCALER",
    },
    {
        FAULT_AMP_FAULTZ, DOM_AUDIO,
        DOM_SCALER, DOM_SCALER, DOM_SCALER,
        DOM_AUDIO,
        "FAULT_AMP_FAULTZ -> SCALER",
    },
    {
        FAULT_ETH1, DOM_ETH1,
        DOM_SCALER, DOM_SCALER, DOM_SCALER,
        DOM_ETH1 | DOM_AUDIO,
        "FAULT_ETH1 -> SCALER",
    },
#if (ENABLE_AUDIO_HW != 0U)
    {
        FAULT_TOUCH, DOM_TOUCH,
        DOM_AUDIO, DOM_AUDIO, DOM_AUDIO,
        DOM_TOUCH | DOM_SCALER,
        "FAULT_TOUCH -> AUDIO",
    },
#else
    {
        FAULT_TOUCH, DOM_TOUCH,
        DOM_ETH1, DOM_ETH1, DOM_ETH1,
        DOM_TOUCH | DOM_SCALER | DOM_AUDIO,
        "FAULT_TOUCH -> ETH1",
    },
#endif
};

void test_cross_domain_matrix_fault_reset_then_independent_power_ctrl(void)
{
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        run_isolation_case(&k_cases[i]);
    }
}

/* ===== End-to-end: FAULT_AUDIO via fault_manager_process, then SCALER ===== */

void test_audio_overcurrent_fault_safe_state_then_scaler_only_recovery(void)
{
    hal_stub_reset();
    power_manager_init();
    fault_manager_init();
    set_v_nominal();
    set_i_nominal();
    seed_valid_display_adc();
    mock_pgood  = 1;
    mock_faultz = 1;
    systick_ms  = 1000;

    power_state = DOM_AUDIO;
    mock_current_ma[3] = (int16_t)THRESH_I_AUDIO_LR_MAX + 1;

    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT; i++) {
        fault_manager_process();
    }

    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_AUDIO);
    assert_full_safe_state();

    fault_clear_flags();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags());
    TEST_ASSERT_EQUAL_HEX8(0, power_state);

    set_i_nominal();
    TEST_ASSERT_EQUAL_UINT8(0, power_ctrl_request(DOM_SCALER, DOM_SCALER));
    tick_ms(SCALER_UP_MS);

    TEST_ASSERT_TRUE(power_state & DOM_SCALER);
    TEST_ASSERT_EQUAL_HEX8(0, power_state & DOM_AUDIO);

    GPIO_PinState st;
    TEST_ASSERT_TRUE(last_gpio_write(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, st);
    TEST_ASSERT_TRUE(last_gpio_write(POWER_AUDIO_GPIO_Port, POWER_AUDIO_Pin, &st));
    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, st);
}

void test_reset_fault_never_auto_enables_other_domain_after_audio_fault(void)
{
    hal_stub_reset();
    power_manager_init();
    fault_manager_init();
    set_v_nominal();
    set_i_nominal();
    systick_ms = 1000;

    power_state = DOM_AUDIO;
    fault_set_flag(FAULT_AUDIO);
    assert_full_safe_state();

    fault_clear_flags();
    for (uint8_t i = 0; i < 30; i++) {
        fault_manager_process();
        power_manager_process();
        systick_ms++;
    }

    TEST_ASSERT_EQUAL_HEX8(0, power_state);
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cross_domain_matrix_fault_reset_then_independent_power_ctrl);
    RUN_TEST(test_audio_overcurrent_fault_safe_state_then_scaler_only_recovery);
    RUN_TEST(test_reset_fault_never_auto_enables_other_domain_after_audio_fault);
    return UNITY_END();
}
