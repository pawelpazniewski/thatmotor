#include "signal_chain.h"
#include "settings_validate.h"
#include "unity.h"

#define SETTLE_CYCLES 2000

static settings_params defaults_params(void)
{
    settings_params p;
    settings_load_defaults(&p);
    return p;
}

static uint32_t servo_center_us(const settings_params *p)
{
    return ((uint32_t)p->servo_min_us + (uint32_t)p->servo_max_us) / 2U;
}

/* Run the servo chain repeatedly so the slew settles; return final pulse. The
 * slew state must start at center (boot / DISARMED neutral). */
static uint32_t settle_servo(uint32_t raw_us, servo_target_mode mode,
                             const settings_params *p)
{
    int32_t slew = (int32_t)servo_center_us(p);
    uint32_t servo_us = 0;
    for (int i = 0; i < SETTLE_CYCLES; i++) {
        servo_us = servo_chain_step(raw_us, mode, p, &slew);
    }
    return servo_us;
}

static void test_full_left_settles_at_min_endpoint(void)
{
    /* Arrange: full one way, RC valid (tracking). */
    settings_params p = defaults_params();

    /* Act: 1000 us -> -full -> servo_min endpoint. */
    uint32_t servo_us = settle_servo(1000U, SERVO_TARGET_TRACK, &p);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(p.servo_min_us, servo_us);
}

static void test_full_right_settles_at_max_endpoint(void)
{
    /* Arrange */
    settings_params p = defaults_params();

    /* Act: 2000 us -> +full -> servo_max endpoint. */
    uint32_t servo_us = settle_servo(2000U, SERVO_TARGET_TRACK, &p);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT32(p.servo_max_us, servo_us);
}

static void test_endpoints_constrain_beyond_calibration(void)
{
    /* Arrange: raw width well beyond the RC max still cannot drive the servo
     * past its configured endpoint (angle is limited to min/max). */
    settings_params p = defaults_params();

    /* Act: absurd width -> still saturates at servo_max. */
    uint32_t servo_us = settle_servo(9000U, SERVO_TARGET_TRACK, &p);

    /* Assert: clamped to the max endpoint (inclusive). */
    TEST_ASSERT_EQUAL_UINT32(p.servo_max_us, servo_us);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(p.servo_max_us, servo_us);
}

static void test_center_input_holds_center(void)
{
    /* Arrange / Act: centered stick. */
    settings_params p = defaults_params();
    uint32_t servo_us = settle_servo(1500U, SERVO_TARGET_TRACK, &p);

    /* Assert: stays at center. */
    TEST_ASSERT_EQUAL_UINT32(servo_center_us(&p), servo_us);
}

static void test_slew_limits_speed_on_large_jump(void)
{
    /* Arrange: start at center, command full right; one cycle only. */
    settings_params p = defaults_params();
    int32_t slew = (int32_t)servo_center_us(&p);

    /* Act: a single large CH1 jump. */
    uint32_t servo_us = servo_chain_step(2000U, SERVO_TARGET_TRACK, &p, &slew);

    /* Assert: moved by exactly the slew rate, NOT all the way to the endpoint. */
    TEST_ASSERT_EQUAL_UINT32(servo_center_us(&p) + p.servo_slew_us_per_cycle,
                             servo_us);
    TEST_ASSERT_LESS_THAN_UINT32(p.servo_max_us, servo_us);
}

static void test_slew_never_overshoots_target(void)
{
    /* Arrange: settle a full-right command. */
    settings_params p = defaults_params();
    int32_t slew = (int32_t)servo_center_us(&p);
    for (int i = 0; i < SETTLE_CYCLES; i++) {
        servo_chain_step(2000U, SERVO_TARGET_TRACK, &p, &slew);
    }

    /* Assert: lands exactly on the endpoint, never past it. */
    TEST_ASSERT_EQUAL_INT32((int32_t)p.servo_max_us, slew);
}

static void test_failsafe_slews_to_center(void)
{
    /* Arrange: servo parked at the right endpoint, then FAILSAFE. */
    settings_params p = defaults_params();
    int32_t slew = (int32_t)p.servo_max_us;

    /* Act: one FAILSAFE cycle must not jump straight to center. */
    uint32_t after_one = servo_chain_step(2000U, SERVO_TARGET_CENTER, &p, &slew);

    /* Assert: still off-center after one cycle (slew, not a jump). */
    TEST_ASSERT_NOT_EQUAL(servo_center_us(&p), after_one);
    TEST_ASSERT_GREATER_THAN_UINT32(servo_center_us(&p), after_one);

    /* Act: settle. */
    for (int i = 0; i < SETTLE_CYCLES; i++) {
        after_one = servo_chain_step(2000U, SERVO_TARGET_CENTER, &p, &slew);
    }

    /* Assert: rests at center. */
    TEST_ASSERT_EQUAL_UINT32(servo_center_us(&p), after_one);
}

static void test_reverse_keeps_center_centered(void)
{
    /* Arrange: servo reverse on, centered stick. Reverse after deadband keeps
     * a centered command centered. */
    settings_params p = defaults_params();
    p.servo_reverse = true;

    /* Act */
    uint32_t servo_us = settle_servo(1500U, SERVO_TARGET_TRACK, &p);

    /* Assert: still center. */
    TEST_ASSERT_EQUAL_UINT32(servo_center_us(&p), servo_us);
}

static void test_reverse_flips_steering_direction(void)
{
    /* Arrange: full-right stick with reverse on should drive to the min side. */
    settings_params p = defaults_params();
    p.servo_reverse = true;

    /* Act */
    uint32_t servo_us = settle_servo(2000U, SERVO_TARGET_TRACK, &p);

    /* Assert: ends at the min endpoint (direction inverted). */
    TEST_ASSERT_EQUAL_UINT32(p.servo_min_us, servo_us);
}

/* Hard SI-3 clamp window for the servo pin (full 270 deg electrical range). */
#define SERVO_HARD_MIN_US 500U
#define SERVO_HARD_MAX_US 2500U

static void test_output_stays_within_clamp_window(void)
{
    /* Arrange: endpoints opened to the full electrical band. */
    settings_params p = defaults_params();
    p.servo_min_us = SERVO_HARD_MIN_US;
    p.servo_max_us = SERVO_HARD_MAX_US;

    /* Act: absurd input. */
    uint32_t servo_us = settle_servo(50000U, SERVO_TARGET_TRACK, &p);

    /* Assert: within the hard clamp window [500, 2500]. */
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(SERVO_HARD_MAX_US, servo_us);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(SERVO_HARD_MIN_US, servo_us);
}

static void test_180deg_endpoints_pass_unclamped(void)
{
    /* Arrange: ~180 deg endpoints (833/2167 us) on a 270 deg servo. The old
     * [1000,2000] clamp would have cut these to 1000/2000; the widened window
     * must let them reach the configured endpoints. */
    settings_params p = defaults_params();
    p.servo_min_us = 833U;
    p.servo_max_us = 2167U;

    /* Act: full left and full right. */
    uint32_t left_us = settle_servo(1000U, SERVO_TARGET_TRACK, &p);
    uint32_t right_us = settle_servo(2000U, SERVO_TARGET_TRACK, &p);

    /* Assert: endpoints reached, NOT clamped to the old 1000/2000 band. */
    TEST_ASSERT_EQUAL_UINT32(833U, left_us);
    TEST_ASSERT_EQUAL_UINT32(2167U, right_us);
}

static void test_hard_window_bounds_extreme_endpoints(void)
{
    /* Arrange: endpoints pushed to the electrical limits. */
    settings_params p = defaults_params();
    p.servo_min_us = SERVO_HARD_MIN_US;
    p.servo_max_us = SERVO_HARD_MAX_US;

    /* Act: settle full left and full right. */
    uint32_t left_us = settle_servo(1000U, SERVO_TARGET_TRACK, &p);
    uint32_t right_us = settle_servo(2000U, SERVO_TARGET_TRACK, &p);

    /* Assert: 500/2500 is the hard boundary and is reachable. */
    TEST_ASSERT_EQUAL_UINT32(SERVO_HARD_MIN_US, left_us);
    TEST_ASSERT_EQUAL_UINT32(SERVO_HARD_MAX_US, right_us);
}

static void test_deploy_settles_at_deploy_servo_us_ignoring_stick(void)
{
    /* Arrange: DEPLOY pins the servo at deploy_servo_us regardless of the CH1
     * stick (drive is off in DEPLOY; the steering input is ignored). */
    settings_params p = defaults_params();
    p.deploy_servo_us = 2167U;

    /* Act: drive a hard-left stick but in DEPLOY mode; settle the slew. */
    uint32_t servo_us = settle_servo(1000U, SERVO_TARGET_DEPLOY, &p);

    /* Assert: rests at the deploy target, not at the steering endpoint. */
    TEST_ASSERT_EQUAL_UINT32(2167U, servo_us);
}

static void test_deploy_target_clamped_to_servo_window(void)
{
    /* Arrange: a deploy target above the hard electrical ceiling (3000 > 2500)
     * — pure unit, so set it directly to prove the SI-3 servo clamp holds even
     * if an out-of-band value ever reaches the chain. */
    settings_params p = defaults_params();
    p.deploy_servo_us = 3000U;

    /* Act */
    uint32_t servo_us = settle_servo(1500U, SERVO_TARGET_DEPLOY, &p);

    /* Assert: snapped to the 2500 us ceiling. */
    TEST_ASSERT_EQUAL_UINT32(2500U, servo_us);
}

/* ---- servo neutral trim (signed output offset, before the hard clamp) ---- */

static void test_trim_shifts_neutral_output(void)
{
    /* Arrange: centered stick, +30 us trim. */
    settings_params p = defaults_params();
    p.servo_trim_us = 30;

    /* Act */
    uint32_t servo_us = settle_servo(1500U, SERVO_TARGET_TRACK, &p);

    /* Assert: neutral output shifted up by exactly the trim. */
    TEST_ASSERT_EQUAL_UINT32(servo_center_us(&p) + 30U, servo_us);
}

static void test_trim_shifts_track_endpoint(void)
{
    /* Arrange: full-right tracking with a +20 us trim. */
    settings_params p = defaults_params();
    p.servo_trim_us = 20;

    /* Act */
    uint32_t servo_us = settle_servo(2000U, SERVO_TARGET_TRACK, &p);

    /* Assert: the max endpoint is shifted up uniformly by the trim. */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)p.servo_max_us + 20U, servo_us);
}

static void test_negative_trim_lowers_output(void)
{
    /* Arrange: centered stick, -40 us trim. */
    settings_params p = defaults_params();
    p.servo_trim_us = -40;

    /* Act */
    uint32_t servo_us = settle_servo(1500U, SERVO_TARGET_TRACK, &p);

    /* Assert: neutral output shifted DOWN by exactly the trim magnitude. */
    TEST_ASSERT_EQUAL_UINT32(servo_center_us(&p) - 40U, servo_us);
}

static void test_trim_cannot_push_past_hard_ceiling(void)
{
    /* Arrange: max endpoint at the electrical ceiling (2500) + 300 us trim must
     * still clamp to 2500, never exceed the SI-3 window. */
    settings_params p = defaults_params();
    p.servo_max_us = 2300U;
    p.servo_trim_us = 300; /* 2300 + 300 = 2600 -> clamp 2500 */

    /* Act */
    uint32_t servo_us = settle_servo(2000U, SERVO_TARGET_TRACK, &p);

    /* Assert: clamped to the hard ceiling. */
    TEST_ASSERT_EQUAL_UINT32(SERVO_HARD_MAX_US, servo_us);
}

static void test_deploy_target_shifted_by_trim(void)
{
    /* Arrange: DEPLOY target with a +50 us trim shifts the held position too. */
    settings_params p = defaults_params();
    p.deploy_servo_us = 2000U;
    p.servo_trim_us = 50;

    /* Act */
    uint32_t servo_us = settle_servo(1500U, SERVO_TARGET_DEPLOY, &p);

    /* Assert: deploy hold + trim. */
    TEST_ASSERT_EQUAL_UINT32(2050U, servo_us);
}

static void test_servo_trim_stepped_adds_and_subtracts(void)
{
    /* dir>0 adds the step, dir<0 subtracts it, dir==0 leaves the value. */
    TEST_ASSERT_EQUAL_INT16(7, servo_trim_stepped(0, 1, 7, 300));
    TEST_ASSERT_EQUAL_INT16(-7, servo_trim_stepped(0, -1, 7, 300));
    TEST_ASSERT_EQUAL_INT16(42, servo_trim_stepped(42, 0, 7, 300));
}

static void test_servo_trim_stepped_clamps_to_symmetric_bound(void)
{
    /* Stepping past +/-max_abs saturates at the bound. */
    TEST_ASSERT_EQUAL_INT16(300, servo_trim_stepped(298, 1, 7, 300));
    TEST_ASSERT_EQUAL_INT16(-300, servo_trim_stepped(-298, -1, 7, 300));
    TEST_ASSERT_EQUAL_INT16(300, servo_trim_stepped(300, 1, 7, 300));
}

void run_servo_chain_tests(void)
{
    RUN_TEST(test_full_left_settles_at_min_endpoint);
    RUN_TEST(test_full_right_settles_at_max_endpoint);
    RUN_TEST(test_endpoints_constrain_beyond_calibration);
    RUN_TEST(test_center_input_holds_center);
    RUN_TEST(test_slew_limits_speed_on_large_jump);
    RUN_TEST(test_slew_never_overshoots_target);
    RUN_TEST(test_failsafe_slews_to_center);
    RUN_TEST(test_reverse_keeps_center_centered);
    RUN_TEST(test_reverse_flips_steering_direction);
    RUN_TEST(test_output_stays_within_clamp_window);
    RUN_TEST(test_180deg_endpoints_pass_unclamped);
    RUN_TEST(test_hard_window_bounds_extreme_endpoints);
    RUN_TEST(test_deploy_settles_at_deploy_servo_us_ignoring_stick);
    RUN_TEST(test_deploy_target_clamped_to_servo_window);
    RUN_TEST(test_trim_shifts_neutral_output);
    RUN_TEST(test_trim_shifts_track_endpoint);
    RUN_TEST(test_negative_trim_lowers_output);
    RUN_TEST(test_trim_cannot_push_past_hard_ceiling);
    RUN_TEST(test_deploy_target_shifted_by_trim);
    RUN_TEST(test_servo_trim_stepped_adds_and_subtracts);
    RUN_TEST(test_servo_trim_stepped_clamps_to_symmetric_bound);
}
