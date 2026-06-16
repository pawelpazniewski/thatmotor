#include "rc_validity.h"
#include "unity.h"

/* Representative thresholds (measured-period placeholder: 20 ms +/- 5 ms). */
static const rc_channel_cfg CFG = {
    .width_min_us = 800U,
    .width_max_us = 2200U,
    .period_expected_us = 20000U,
    .period_tol_us = 5000U,
    .edge_timeout_us = 30000U,
};

/* Capture-counter resolution: 80 MHz -> 80 ticks per microsecond. now_ticks and
 * last_edge_ticks live in this same domain (NOT esp_timer microseconds). */
#define TICKS_PER_US 80U

/* "Now" as a capture-counter tick, mid-range so it is far from a wrap edge. */
#define NOW_TICKS 1000000U

static rc_channel_sample valid_sample(void)
{
    rc_channel_sample s = {
        .width_us = 1500U,
        .period_us = 20000U,
        /* edge 1 ms ago = 1000 us * 80 ticks/us = 80000 ticks earlier */
        .last_edge_ticks = NOW_TICKS - (1000U * TICKS_PER_US),
        .edge_seen = true,
    };
    return s;
}

static void test_valid_pulse_is_valid(void)
{
    /* Arrange */
    rc_channel_sample s = valid_sample();

    /* Act */
    bool result = channel_valid(&s, NOW_TICKS, &CFG);

    /* Assert */
    TEST_ASSERT_TRUE(result);
}

static void test_no_recent_edge_is_invalid(void)
{
    /* Arrange: last edge well beyond edge_timeout_us (100 ms ago). */
    rc_channel_sample s = valid_sample();
    s.last_edge_ticks = NOW_TICKS - (100000U * TICKS_PER_US);

    /* Act / Assert */
    TEST_ASSERT_FALSE(channel_valid(&s, NOW_TICKS, &CFG));
}

static void test_never_seen_edge_is_invalid(void)
{
    /* Arrange */
    rc_channel_sample s = valid_sample();
    s.edge_seen = false;

    /* Act / Assert */
    TEST_ASSERT_FALSE(channel_valid(&s, NOW_TICKS, &CFG));
}

static void test_recency_correct_across_counter_wrap(void)
{
    /* Arrange: edge stamped just before the 32-bit counter wraps, "now" just
     * after the wrap. Elapsed = 1000 us -> within the 30 ms timeout. A naive
     * subtraction in the us domain would mis-judge this; modular tick
     * subtraction must still report recent. */
    rc_channel_sample s = valid_sample();
    uint32_t now_ticks = 1000U * TICKS_PER_US - 1U; /* just past wrap */
    s.last_edge_ticks = now_ticks - (1000U * TICKS_PER_US); /* wraps below 0 */

    /* Act / Assert: edge is 1 ms old across the wrap -> still valid. */
    TEST_ASSERT_TRUE(channel_valid(&s, now_ticks, &CFG));
}

static void test_stale_edge_across_wrap_is_invalid(void)
{
    /* Arrange: same wrap geometry but the edge is 100 ms old (> timeout). */
    rc_channel_sample s = valid_sample();
    uint32_t now_ticks = 1000U * TICKS_PER_US - 1U;
    s.last_edge_ticks = now_ticks - (100000U * TICKS_PER_US);

    /* Act / Assert: too old even across the wrap -> not recent. */
    TEST_ASSERT_FALSE(channel_valid(&s, now_ticks, &CFG));
}

static void test_width_too_low_is_invalid(void)
{
    /* Arrange: 700 us is below the 800 us floor. */
    rc_channel_sample s = valid_sample();
    s.width_us = 700U;

    /* Act / Assert */
    TEST_ASSERT_FALSE(channel_valid(&s, NOW_TICKS, &CFG));
}

static void test_width_too_high_is_invalid(void)
{
    /* Arrange: 2300 us is above the 2200 us ceiling. */
    rc_channel_sample s = valid_sample();
    s.width_us = 2300U;

    /* Act / Assert */
    TEST_ASSERT_FALSE(channel_valid(&s, NOW_TICKS, &CFG));
}

static void test_width_at_min_boundary_is_valid(void)
{
    /* Arrange: width exactly at width_min_us (inclusive boundary). */
    rc_channel_sample s = valid_sample();
    s.width_us = CFG.width_min_us;

    /* Act / Assert */
    TEST_ASSERT_TRUE(channel_valid(&s, NOW_TICKS, &CFG));
}

static void test_width_at_max_boundary_is_valid(void)
{
    /* Arrange: width exactly at width_max_us (inclusive boundary). */
    rc_channel_sample s = valid_sample();
    s.width_us = CFG.width_max_us;

    /* Act / Assert */
    TEST_ASSERT_TRUE(channel_valid(&s, NOW_TICKS, &CFG));
}

static void test_period_out_of_tolerance_is_invalid(void)
{
    /* Arrange: 30 ms period is 10 ms off expected, beyond 5 ms tolerance. */
    rc_channel_sample s = valid_sample();
    s.period_us = 30000U;

    /* Act / Assert */
    TEST_ASSERT_FALSE(channel_valid(&s, NOW_TICKS, &CFG));
}

static void test_period_at_upper_tolerance_boundary_is_valid(void)
{
    /* Arrange: period exactly at expected + tolerance (inclusive). */
    rc_channel_sample s = valid_sample();
    s.period_us = CFG.period_expected_us + CFG.period_tol_us;

    /* Act / Assert */
    TEST_ASSERT_TRUE(channel_valid(&s, NOW_TICKS, &CFG));
}

static void test_period_at_lower_tolerance_boundary_is_valid(void)
{
    /* Arrange: period exactly at expected - tolerance (inclusive). */
    rc_channel_sample s = valid_sample();
    s.period_us = CFG.period_expected_us - CFG.period_tol_us;

    /* Act / Assert */
    TEST_ASSERT_TRUE(channel_valid(&s, NOW_TICKS, &CFG));
}

static void test_period_one_us_past_tolerance_is_invalid(void)
{
    /* Arrange: one microsecond beyond the upper tolerance bound. */
    rc_channel_sample s = valid_sample();
    s.period_us = CFG.period_expected_us + CFG.period_tol_us + 1U;

    /* Act / Assert */
    TEST_ASSERT_FALSE(channel_valid(&s, NOW_TICKS, &CFG));
}

static void test_rc_valid_both_good(void)
{
    /* Act / Assert: both channels valid -> RC_valid. */
    TEST_ASSERT_TRUE(rc_valid(true, true));
}

static void test_rc_valid_ch1_bad(void)
{
    TEST_ASSERT_FALSE(rc_valid(false, true));
}

static void test_rc_valid_ch2_bad(void)
{
    TEST_ASSERT_FALSE(rc_valid(true, false));
}

static void test_rc_valid_ignores_ch4(void)
{
    /* rc_valid only takes CH1 AND CH2; CH4 is never an argument, so a bad CH4
     * cannot affect the result. Both control channels good -> still valid. */
    TEST_ASSERT_TRUE(rc_valid(true, true));
}

static void test_debounce_single_bad_frame_stays_valid(void)
{
    /* Arrange */
    rc_debounce_state state;
    rc_debounce_init(&state, RC_DEBOUNCE_DEFAULT_THRESHOLD);

    /* Act: one invalid frame after init. */
    bool result = rc_debounce_update(&state, false);

    /* Assert: not enough to trip the threshold. */
    TEST_ASSERT_TRUE(result);
}

static void test_debounce_n_consecutive_bad_latches_invalid(void)
{
    /* Arrange */
    rc_debounce_state state;
    rc_debounce_init(&state, RC_DEBOUNCE_DEFAULT_THRESHOLD);

    /* Act: feed exactly threshold consecutive invalid frames. */
    bool result = true;
    for (unsigned i = 0; i < RC_DEBOUNCE_DEFAULT_THRESHOLD; i++) {
        result = rc_debounce_update(&state, false);
    }

    /* Assert: latched invalid. */
    TEST_ASSERT_FALSE(result);
}

static void test_debounce_good_frame_resets_counter(void)
{
    /* Arrange: accumulate threshold-1 bad frames (still valid). */
    rc_debounce_state state;
    rc_debounce_init(&state, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    for (unsigned i = 0; i < RC_DEBOUNCE_DEFAULT_THRESHOLD - 1U; i++) {
        rc_debounce_update(&state, false);
    }

    /* Act: a good frame resets the counter; then one bad frame must not latch. */
    rc_debounce_update(&state, true);
    bool result = rc_debounce_update(&state, false);

    /* Assert: still valid because the counter was reset. */
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT16(1U, state.bad_frame_count);
}

void run_rc_validity_tests(void)
{
    RUN_TEST(test_valid_pulse_is_valid);
    RUN_TEST(test_no_recent_edge_is_invalid);
    RUN_TEST(test_never_seen_edge_is_invalid);
    RUN_TEST(test_recency_correct_across_counter_wrap);
    RUN_TEST(test_stale_edge_across_wrap_is_invalid);
    RUN_TEST(test_width_too_low_is_invalid);
    RUN_TEST(test_width_too_high_is_invalid);
    RUN_TEST(test_width_at_min_boundary_is_valid);
    RUN_TEST(test_width_at_max_boundary_is_valid);
    RUN_TEST(test_period_out_of_tolerance_is_invalid);
    RUN_TEST(test_period_at_upper_tolerance_boundary_is_valid);
    RUN_TEST(test_period_at_lower_tolerance_boundary_is_valid);
    RUN_TEST(test_period_one_us_past_tolerance_is_invalid);
    RUN_TEST(test_rc_valid_both_good);
    RUN_TEST(test_rc_valid_ch1_bad);
    RUN_TEST(test_rc_valid_ch2_bad);
    RUN_TEST(test_rc_valid_ignores_ch4);
    RUN_TEST(test_debounce_single_bad_frame_stays_valid);
    RUN_TEST(test_debounce_n_consecutive_bad_latches_invalid);
    RUN_TEST(test_debounce_good_frame_resets_counter);
}
