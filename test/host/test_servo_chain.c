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

static void test_output_stays_within_clamp_window(void)
{
    /* Arrange: endpoints opened to the full actuator band. */
    settings_params p = defaults_params();
    p.servo_min_us = 1000U;
    p.servo_max_us = 2000U;

    /* Act: absurd input. */
    uint32_t servo_us = settle_servo(50000U, SERVO_TARGET_TRACK, &p);

    /* Assert: within the hard clamp window [1000, 2000]. */
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(2000U, servo_us);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1000U, servo_us);
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
}
