#include "pwm_us_to_duty.h"
#include "safety_clamp.h"
#include "unity.h"

/* Expected 14-bit duty values at 50 Hz for the standard RC band (ESP32-S3). */
#define DUTY_1000US 819U
#define DUTY_1500US 1229U
#define DUTY_2000US 1638U

static void test_1000us_maps_to_819(void)
{
    /* Arrange / Act */
    uint32_t duty = pwm_us_to_duty(1000U);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(DUTY_1000US, duty);
}

static void test_1500us_maps_to_1229(void)
{
    /* Arrange / Act */
    uint32_t duty = pwm_us_to_duty(1500U);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(DUTY_1500US, duty);
}

static void test_2000us_maps_to_1638(void)
{
    /* Arrange / Act */
    uint32_t duty = pwm_us_to_duty(2000U);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(DUTY_2000US, duty);
}

static void test_out_of_window_value_clamped_first_then_converted(void)
{
    /* Arrange: a value above the safe window must be clamped before conversion
     * so the resulting duty equals the duty of the window maximum, never higher. */
    const PwmWindow window = {.min_us = 1000U, .max_us = 2000U};

    /* Act */
    uint32_t clamped = clamp_pwm_us(5000U, window);
    uint32_t duty = pwm_us_to_duty(clamped);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(DUTY_2000US, duty);
}

static void test_below_window_value_clamped_first_then_converted(void)
{
    /* Arrange */
    const PwmWindow window = {.min_us = 1000U, .max_us = 2000U};

    /* Act */
    uint32_t clamped = clamp_pwm_us(200U, window);
    uint32_t duty = pwm_us_to_duty(clamped);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(DUTY_1000US, duty);
}

void run_pwm_us_to_duty_tests(void)
{
    RUN_TEST(test_1000us_maps_to_819);
    RUN_TEST(test_1500us_maps_to_1229);
    RUN_TEST(test_2000us_maps_to_1638);
    RUN_TEST(test_out_of_window_value_clamped_first_then_converted);
    RUN_TEST(test_below_window_value_clamped_first_then_converted);
}
