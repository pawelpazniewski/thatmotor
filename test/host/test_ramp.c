#include "ramp.h"
#include "unity.h"

static void test_ramp_up_steps_toward_target_by_rate_up(void)
{
    /* Arrange: rising command from 0 toward 100, rate_up 5. */
    /* Act */
    int32_t next = ramp_step(0, 100, 5, 10);

    /* Assert: moved up by exactly rate_up. */
    TEST_ASSERT_EQUAL_INT32(5, next);
}

static void test_ramp_down_steps_toward_target_by_rate_down(void)
{
    /* Arrange: falling command from 100 toward 0, rate_down 10. */
    /* Act */
    int32_t next = ramp_step(100, 0, 5, 10);

    /* Assert: moved down by exactly rate_down (separate from rate_up). */
    TEST_ASSERT_EQUAL_INT32(90, next);
}

static void test_ramp_up_and_down_rates_are_independent(void)
{
    /* Arrange: same distance up vs down with asymmetric rates. */
    /* Act */
    int32_t up = ramp_step(0, 100, 5, 10);
    int32_t down = ramp_step(100, 0, 5, 10);

    /* Assert: up advanced 5, down retreated 10 -> rates not shared. */
    TEST_ASSERT_EQUAL_INT32(5, up);
    TEST_ASSERT_EQUAL_INT32(90, down);
}

static void test_ramp_never_overshoots_target_rising(void)
{
    /* Arrange: target one unit away but rate larger than the gap. */
    /* Act */
    int32_t next = ramp_step(99, 100, 50, 50);

    /* Assert: lands exactly on target, does not pass it. */
    TEST_ASSERT_EQUAL_INT32(100, next);
}

static void test_ramp_never_overshoots_target_falling(void)
{
    /* Arrange: target one unit below but rate larger than the gap. */
    /* Act */
    int32_t next = ramp_step(1, 0, 50, 50);

    /* Assert: lands exactly on target, does not pass it. */
    TEST_ASSERT_EQUAL_INT32(0, next);
}

static void test_ramp_at_target_holds(void)
{
    /* Arrange / Act: already at target. */
    int32_t next = ramp_step(42, 42, 5, 10);

    /* Assert: unchanged. */
    TEST_ASSERT_EQUAL_INT32(42, next);
}

static void test_ramp_reaches_max_over_multiple_cycles(void)
{
    /* Arrange: ramp 0 -> 1000 (full scale) with rate_up 200. */
    int32_t value = 0;

    /* Act: iterate until settled (bounded loop). */
    for (int i = 0; i < 100; i++) {
        value = ramp_step(value, 1000, 200, 100);
    }

    /* Assert: arrived exactly at target without overshoot. */
    TEST_ASSERT_EQUAL_INT32(1000, value);
}

static void test_slew_is_symmetric(void)
{
    /* Arrange / Act: same rate up and down. */
    int32_t up = slew_step(0, 100, 7);
    int32_t down = slew_step(100, 0, 7);

    /* Assert: equal magnitude step in both directions. */
    TEST_ASSERT_EQUAL_INT32(7, up);
    TEST_ASSERT_EQUAL_INT32(93, down);
}

static void test_slew_never_overshoots(void)
{
    /* Arrange / Act: gap smaller than rate. */
    int32_t next = slew_step(1498, 1500, 10);

    /* Assert: lands on target exactly. */
    TEST_ASSERT_EQUAL_INT32(1500, next);
}

void run_ramp_tests(void)
{
    RUN_TEST(test_ramp_up_steps_toward_target_by_rate_up);
    RUN_TEST(test_ramp_down_steps_toward_target_by_rate_down);
    RUN_TEST(test_ramp_up_and_down_rates_are_independent);
    RUN_TEST(test_ramp_never_overshoots_target_rising);
    RUN_TEST(test_ramp_never_overshoots_target_falling);
    RUN_TEST(test_ramp_at_target_holds);
    RUN_TEST(test_ramp_reaches_max_over_multiple_cycles);
    RUN_TEST(test_slew_is_symmetric);
    RUN_TEST(test_slew_never_overshoots);
}
