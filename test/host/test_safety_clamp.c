#include "safety_clamp.h"
#include "unity.h"

static const PwmWindow WINDOW = {.min_us = 1000U, .max_us = 2000U};

static void test_below_min_clamps_to_min(void)
{
    /* Arrange / Act */
    uint32_t result = clamp_pwm_us(500U, WINDOW);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(WINDOW.min_us, result);
}

static void test_above_max_clamps_to_max(void)
{
    /* Arrange / Act */
    uint32_t result = clamp_pwm_us(3000U, WINDOW);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(WINDOW.max_us, result);
}

static void test_inside_window_passes_unchanged(void)
{
    /* Arrange / Act */
    uint32_t result = clamp_pwm_us(1500U, WINDOW);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(1500U, result);
}

static void test_min_boundary_inclusive(void)
{
    /* Arrange / Act */
    uint32_t result = clamp_pwm_us(WINDOW.min_us, WINDOW);

    /* Assert: boundary is inclusive, value is unchanged. */
    TEST_ASSERT_EQUAL_UINT32(WINDOW.min_us, result);
}

static void test_max_boundary_inclusive(void)
{
    /* Arrange / Act */
    uint32_t result = clamp_pwm_us(WINDOW.max_us, WINDOW);

    /* Assert: boundary is inclusive, value is unchanged. */
    TEST_ASSERT_EQUAL_UINT32(WINDOW.max_us, result);
}

void run_safety_clamp_tests(void)
{
    RUN_TEST(test_below_min_clamps_to_min);
    RUN_TEST(test_above_max_clamps_to_max);
    RUN_TEST(test_inside_window_passes_unchanged);
    RUN_TEST(test_min_boundary_inclusive);
    RUN_TEST(test_max_boundary_inclusive);
}
