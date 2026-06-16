#include "cap_math.h"
#include "unity.h"

/* 80 MHz capture clock: 80 ticks per microsecond, 12.5 ns/tick. */
#define TICKS_PER_US 80U

static void test_ticks_to_us_one_microsecond(void)
{
    /* Arrange / Act: 80 ticks at 12.5 ns each == 1000 ns == 1 us. */
    uint32_t us = cap_ticks_to_us(TICKS_PER_US);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(1U, us);
}

static void test_ticks_to_us_typical_pulse(void)
{
    /* Arrange: a 1500 us neutral pulse is 120000 ticks. */
    uint32_t ticks = 1500U * TICKS_PER_US;

    /* Act */
    uint32_t us = cap_ticks_to_us(ticks);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(1500U, us);
}

static void test_ticks_to_us_rounds_to_nearest(void)
{
    /* Arrange: 40 ticks == 500 ns == 0.5 us -> rounds up to 1. */
    /* Act */
    uint32_t us = cap_ticks_to_us(40U);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(1U, us);
}

static void test_period_from_two_edges(void)
{
    /* Arrange: rising edges 20 ms apart -> 1600000 ticks apart. */
    uint32_t prev = 1000U;
    uint32_t now = prev + 20000U * TICKS_PER_US;

    /* Act */
    uint32_t period = cap_period_us(now, prev);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(20000U, period);
}

static void test_ticks_elapsed_handles_counter_overflow(void)
{
    /* Arrange: previous edge just before the 32-bit wrap, current edge after
     * wrap. Elapsed must be the true gap (10 ticks), not a huge value. */
    uint32_t prev = 0xFFFFFFF6U; /* 2^32 - 10 */
    uint32_t now = 4U;           /* wrapped past zero */

    /* Act */
    uint32_t elapsed = cap_ticks_elapsed(now, prev);

    /* Assert: 10 ticks before wrap + 4 after = 14 ticks. */
    TEST_ASSERT_EQUAL_UINT32(14U, elapsed);
}

static void test_period_us_across_overflow(void)
{
    /* Arrange: a 20 ms frame straddling the counter wrap. */
    uint32_t span_ticks = 20000U * TICKS_PER_US;
    uint32_t prev = 0xFFFFFFFFU - (span_ticks / 2U);
    uint32_t now = prev + span_ticks; /* wraps */

    /* Act */
    uint32_t period = cap_period_us(now, prev);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(20000U, period);
}

void run_cap_math_tests(void)
{
    RUN_TEST(test_ticks_to_us_one_microsecond);
    RUN_TEST(test_ticks_to_us_typical_pulse);
    RUN_TEST(test_ticks_to_us_rounds_to_nearest);
    RUN_TEST(test_period_from_two_edges);
    RUN_TEST(test_ticks_elapsed_handles_counter_overflow);
    RUN_TEST(test_period_us_across_overflow);
}
