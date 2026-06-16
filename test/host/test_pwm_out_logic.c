#include "pwm_out_logic.h"
#include "pwm_us_to_duty.h"
#include "safety_clamp.h"
#include "unity.h"

/* Conservative RC window mirroring the production PWM_OUT_WINDOW. */
static const PwmWindow WINDOW = {.min_us = 900U, .max_us = 2100U};

/* Expected 16-bit duty values at 50 Hz. */
#define DUTY_1500US 4915U
#define DUTY_MAX_US_2100 6881U

static void test_valid_channel_resolves_clamped_duty(void)
{
    /* Arrange: a value inside the window on a valid channel (happy path). */
    uint32_t duty = 0;

    /* Act */
    PwmOutLogicResult result = pwm_out_resolve_duty(0, 1500U, WINDOW, &duty);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_OK, result);
    TEST_ASSERT_EQUAL_UINT32(DUTY_1500US, duty);
}

static void test_invalid_channel_too_high_returns_error(void)
{
    /* Arrange */
    uint32_t duty = 0;

    /* Act: channel equal to the count is out of range (error case). */
    PwmOutLogicResult result =
        pwm_out_resolve_duty(PWM_OUT_LOGIC_CHANNEL_COUNT, 1500U, WINDOW, &duty);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_INVALID_CHANNEL, result);
}

static void test_invalid_channel_negative_returns_error(void)
{
    /* Arrange */
    uint32_t duty = 0;

    /* Act */
    PwmOutLogicResult result = pwm_out_resolve_duty(-1, 1500U, WINDOW, &duty);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_INVALID_CHANNEL, result);
}

static void test_above_window_is_clamped_before_convert(void)
{
    /* Arrange: a value far above the window. The production resolve path must
     * clamp to PWM_OUT max (2100us) BEFORE conversion, never emitting a higher
     * duty. This proves clamp-before-convert on the real function, not via a
     * hand-composed test. */
    uint32_t duty = 0;

    /* Act */
    PwmOutLogicResult result = pwm_out_resolve_duty(1, 9000U, WINDOW, &duty);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_OK, result);
    TEST_ASSERT_EQUAL_UINT32(DUTY_MAX_US_2100, duty);
}

void run_pwm_out_logic_tests(void)
{
    RUN_TEST(test_valid_channel_resolves_clamped_duty);
    RUN_TEST(test_invalid_channel_too_high_returns_error);
    RUN_TEST(test_invalid_channel_negative_returns_error);
    RUN_TEST(test_above_window_is_clamped_before_convert);
}
