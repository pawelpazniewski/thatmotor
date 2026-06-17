#include "pwm_out_logic.h"
#include "pwm_us_to_duty.h"
#include "safety_clamp.h"
#include "unity.h"

/* Conservative RC window mirroring the original shared PWM window. */
static const PwmWindow WINDOW = {.min_us = 900U, .max_us = 2100U};

/* Production per-channel windows (servo full 270 deg range, ESC narrow band). */
static const PwmWindow SERVO_WINDOW = {.min_us = 500U, .max_us = 2500U};
static const PwmWindow ESC_WINDOW = {.min_us = 1000U, .max_us = 2000U};

/* Logical channel indices (mirror PwmOutChannel without linking pwm_out.h). */
#define CHANNEL_SERVO 0
#define CHANNEL_ESC 1

/* Expected 16-bit duty values at 50 Hz. */
#define DUTY_1500US 4915U
#define DUTY_MAX_US_2100 6881U
#define DUTY_833US 2730U
#define DUTY_2167US 7101U
#define DUTY_500US 1638U
#define DUTY_2500US 8192U
#define DUTY_1000US 3277U
#define DUTY_2000US 6554U
#define DUTY_2300US 7537U

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

static void test_esc_above_band_clamps_to_2000(void)
{
    /* Arrange: ESC must hold the narrow [1000,2000] band (WP880 safety). */
    uint32_t duty = 0;

    /* Act: 2400 us is above the ESC max. */
    PwmOutLogicResult result =
        pwm_out_resolve_duty(CHANNEL_ESC, 2400U, ESC_WINDOW, &duty);

    /* Assert: clamped down to 2000 us. */
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_OK, result);
    TEST_ASSERT_EQUAL_UINT32(DUTY_2000US, duty);
}

static void test_esc_below_band_clamps_to_1000(void)
{
    /* Arrange */
    uint32_t duty = 0;

    /* Act: 800 us is below the ESC min. */
    PwmOutLogicResult result =
        pwm_out_resolve_duty(CHANNEL_ESC, 800U, ESC_WINDOW, &duty);

    /* Assert: clamped up to 1000 us. */
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_OK, result);
    TEST_ASSERT_EQUAL_UINT32(DUTY_1000US, duty);
}

static void test_servo_833us_passes_unclamped(void)
{
    /* Arrange: 833 us (a ~180 deg low endpoint) sits inside the servo window
     * and must NOT be clamped up to the old 1000 us floor. */
    uint32_t duty = 0;

    /* Act */
    PwmOutLogicResult result =
        pwm_out_resolve_duty(CHANNEL_SERVO, 833U, SERVO_WINDOW, &duty);

    /* Assert: converted as-is, not clamped. */
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_OK, result);
    TEST_ASSERT_EQUAL_UINT32(DUTY_833US, duty);
}

static void test_servo_2167us_passes_unclamped(void)
{
    /* Arrange: 2167 us (a ~180 deg high endpoint) sits inside the servo window
     * and must NOT be clamped down to the old 2000 us ceiling. */
    uint32_t duty = 0;

    /* Act */
    PwmOutLogicResult result =
        pwm_out_resolve_duty(CHANNEL_SERVO, 2167U, SERVO_WINDOW, &duty);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_OK, result);
    TEST_ASSERT_EQUAL_UINT32(DUTY_2167US, duty);
}

static void test_servo_below_band_clamps_to_500(void)
{
    /* Arrange: 400 us is below the servo electrical floor. */
    uint32_t duty = 0;

    /* Act */
    PwmOutLogicResult result =
        pwm_out_resolve_duty(CHANNEL_SERVO, 400U, SERVO_WINDOW, &duty);

    /* Assert: clamped up to 500 us. */
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_OK, result);
    TEST_ASSERT_EQUAL_UINT32(DUTY_500US, duty);
}

static void test_servo_above_band_clamps_to_2500(void)
{
    /* Arrange: 2600 us is above the servo electrical ceiling. */
    uint32_t duty = 0;

    /* Act */
    PwmOutLogicResult result =
        pwm_out_resolve_duty(CHANNEL_SERVO, 2600U, SERVO_WINDOW, &duty);

    /* Assert: clamped down to 2500 us. */
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_OK, result);
    TEST_ASSERT_EQUAL_UINT32(DUTY_2500US, duty);
}

static void test_same_value_differs_per_channel_window(void)
{
    /* Oracle: the SAME requested pulse must resolve DIFFERENTLY for SERVO vs
     * ESC, proving the windows are genuinely per-channel. 2300 us is inside the
     * servo window (passes) but above the ESC band (clamped to 2000). If a
     * shared window ever returned, both branches would match and this fails. */
    uint32_t servo_duty = 0;
    uint32_t esc_duty = 0;

    /* Act */
    PwmOutLogicResult servo_result =
        pwm_out_resolve_duty(CHANNEL_SERVO, 2300U, SERVO_WINDOW, &servo_duty);
    PwmOutLogicResult esc_result =
        pwm_out_resolve_duty(CHANNEL_ESC, 2300U, ESC_WINDOW, &esc_duty);

    /* Assert: servo keeps 2300 us, ESC is clamped to 2000 us; they differ. */
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_OK, servo_result);
    TEST_ASSERT_EQUAL_INT(PWM_OUT_LOGIC_OK, esc_result);
    TEST_ASSERT_EQUAL_UINT32(DUTY_2300US, servo_duty);
    TEST_ASSERT_EQUAL_UINT32(DUTY_2000US, esc_duty);
    TEST_ASSERT_NOT_EQUAL(servo_duty, esc_duty);
}

void run_pwm_out_logic_tests(void)
{
    RUN_TEST(test_valid_channel_resolves_clamped_duty);
    RUN_TEST(test_invalid_channel_too_high_returns_error);
    RUN_TEST(test_invalid_channel_negative_returns_error);
    RUN_TEST(test_above_window_is_clamped_before_convert);
    RUN_TEST(test_esc_above_band_clamps_to_2000);
    RUN_TEST(test_esc_below_band_clamps_to_1000);
    RUN_TEST(test_servo_833us_passes_unclamped);
    RUN_TEST(test_servo_2167us_passes_unclamped);
    RUN_TEST(test_servo_below_band_clamps_to_500);
    RUN_TEST(test_servo_above_band_clamps_to_2500);
    RUN_TEST(test_same_value_differs_per_channel_window);
}
