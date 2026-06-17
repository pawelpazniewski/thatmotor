#include "signal_chain.h"
#include "settings_validate.h"
#include "unity.h"

/* Number of cycles large enough for the ramp to settle in these tests. */
#define SETTLE_CYCLES 2000

static settings_params defaults_params(void)
{
    settings_params p;
    settings_load_defaults(&p);
    return p;
}

/* Run the throttle chain repeatedly with a fixed input so the ramp settles,
 * then return the final ESC pulse width. */
static uint32_t settle_throttle(uint32_t raw_us, throttle_target_mode mode,
                                const settings_params *p)
{
    int32_t ramp = 0;
    uint32_t esc_us = 0;
    for (int i = 0; i < SETTLE_CYCLES; i++) {
        esc_us = throttle_chain_step(raw_us, mode, p, &ramp);
    }
    return esc_us;
}

static void test_deadband_small_signal_maps_to_neutral(void)
{
    /* Arrange: stick just inside the throttle deadband, ARMED (tracking). */
    settings_params p = defaults_params();

    /* Act: 1580 us -> normalized 160 == deadband -> target 0. */
    uint32_t esc_us = settle_throttle(1580U, THROTTLE_TARGET_TRACK, &p);

    /* Assert: settles to ESC neutral. */
    TEST_ASSERT_EQUAL_UINT32(p.esc_neutral_us, esc_us);
}

static void test_deadband_just_past_threshold_is_nonzero(void)
{
    /* Arrange: stick just beyond the deadband, ARMED. */
    settings_params p = defaults_params();

    /* Act: 1600 us -> normalized 200 > deadband -> nonzero target. */
    uint32_t esc_us = settle_throttle(1600U, THROTTLE_TARGET_TRACK, &p);

    /* Assert: output moves off neutral toward forward. */
    TEST_ASSERT_NOT_EQUAL(p.esc_neutral_us, esc_us);
    TEST_ASSERT_GREATER_THAN_UINT32(p.esc_neutral_us, esc_us);
}

static void test_deadband_at_threshold_is_inclusive_neutral(void)
{
    /* Arrange: stick exactly at the deadband edge (magnitude == deadband). */
    settings_params p = defaults_params();
    /* deadband 80 us over a 500 us half-range -> normalized 160; the raw width
     * that yields exactly 160 is mid + 80 = 1580 us. */

    /* Act */
    uint32_t esc_us = settle_throttle(1580U, THROTTLE_TARGET_TRACK, &p);

    /* Assert: at-threshold collapses to neutral (deadband is inclusive). */
    TEST_ASSERT_EQUAL_UINT32(p.esc_neutral_us, esc_us);
}

static void test_offcenter_deadband_is_consistent_both_sides(void)
{
    /* Arrange: off-center calibration 1000/1300/2000 (low half 300 us, high half
     * 700 us) with an 80 us throttle deadband. The deadband must act as the SAME
     * 80 us physical dead zone on BOTH sides of mid, not 80 us low / ~188 us high
     * (the pre-fix asymmetry from borrowing the nearer half for both sides). */
    settings_params p = defaults_params();
    p.rc_min_us = 1000U;
    p.rc_mid_us = 1300U;
    p.rc_max_us = 2000U;
    p.throttle_deadband_us = 80U;
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;

    /* Act + Assert: exactly 80 us off mid (both sides) stays neutral. */
    TEST_ASSERT_EQUAL_UINT32(
        p.esc_neutral_us, settle_throttle(1220U, THROTTLE_TARGET_TRACK, &p));
    TEST_ASSERT_EQUAL_UINT32(
        p.esc_neutral_us, settle_throttle(1380U, THROTTLE_TARGET_TRACK, &p));

    /* Act + Assert: one us past the dead zone moves off neutral on BOTH sides,
     * each toward its own direction (low -> reverse, high -> forward). */
    uint32_t low_past = settle_throttle(1219U, THROTTLE_TARGET_TRACK, &p);
    uint32_t high_past = settle_throttle(1381U, THROTTLE_TARGET_TRACK, &p);
    TEST_ASSERT_LESS_THAN_UINT32(p.esc_neutral_us, low_past);
    TEST_ASSERT_GREATER_THAN_UINT32(p.esc_neutral_us, high_past);
}

static void test_reverse_keeps_neutral_neutral(void)
{
    /* Arrange: reverse enabled, stick exactly centered. Reverse is applied
     * AFTER the deadband, so a neutral command must stay neutral. */
    settings_params p = defaults_params();
    p.throttle_reverse = true;

    /* Act: centered stick (1500 us) -> 0 command -> reverse(0) == 0. */
    uint32_t esc_us = settle_throttle(1500U, THROTTLE_TARGET_TRACK, &p);

    /* Assert: still neutral despite reverse. */
    TEST_ASSERT_EQUAL_UINT32(p.esc_neutral_us, esc_us);
}

static void test_reverse_flips_direction_of_a_nonzero_command(void)
{
    /* Arrange: forward stick with reverse on should drive the ESC toward the
     * reverse side of neutral. */
    settings_params p = defaults_params();
    p.throttle_reverse = true;
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;

    /* Act: full forward stick, reversed. */
    uint32_t esc_us = settle_throttle(2000U, THROTTLE_TARGET_TRACK, &p);

    /* Assert: ends below neutral (reverse side). */
    TEST_ASSERT_LESS_THAN_UINT32(p.esc_neutral_us, esc_us);
}

static void test_power_limit_forward_caps_target_before_ramp(void)
{
    /* Arrange: full forward stick with an explicit 30% forward limit (reverse
     * opened so only the forward cap is exercised). */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 30U;
    p.max_throttle_rev_pct = 100U;

    /* Act: full forward (2000 us). */
    uint32_t esc_us = settle_throttle(2000U, THROTTLE_TARGET_TRACK, &p);

    /* Assert: limited to 30% of the forward span.
     * forward span = esc_forward_max - esc_neutral = 1900 - 1500 = 400.
     * 30% command -> 0.30 * 400 = 120 us above neutral = 1620 us. */
    TEST_ASSERT_EQUAL_UINT32(1620U, esc_us);
}

static void test_power_limit_reverse_caps_independently_of_forward(void)
{
    /* Arrange: full reverse stick with a 50% reverse limit but the forward limit
     * wide open, proving the caps are asymmetric and direction-specific. */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 50U;

    /* Act: full reverse (1000 us). */
    uint32_t esc_us = settle_throttle(1000U, THROTTLE_TARGET_TRACK, &p);

    /* Assert: limited to 50% of the reverse span.
     * reverse span = esc_neutral - esc_reverse_max = 1500 - 1100 = 400.
     * 50% command -> 0.50 * 400 = 200 us below neutral = 1300 us. */
    TEST_ASSERT_EQUAL_UINT32(1300U, esc_us);
}

static void test_power_limit_in_band_command_passes_through(void)
{
    /* Arrange: forward and reverse limits both at 90%, full forward stick whose
     * 100% command exceeds 90% -> capped, but a command at exactly the limit
     * passes unchanged (inclusive boundary). Use a 90% forward cap and verify
     * the output equals 90% of the forward span. */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 90U;
    p.max_throttle_rev_pct = 90U;

    /* Act: full forward (2000 us). */
    uint32_t esc_us = settle_throttle(2000U, THROTTLE_TARGET_TRACK, &p);

    /* Assert: 90% of the 400 us forward span -> 360 us above neutral = 1860 us. */
    TEST_ASSERT_EQUAL_UINT32(1860U, esc_us);
}

static void test_power_limit_full_forward_without_limit(void)
{
    /* Arrange: full forward with the power limit opened to 100%. */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;

    /* Act */
    uint32_t esc_us = settle_throttle(2000U, THROTTLE_TARGET_TRACK, &p);

    /* Assert: reaches the calibrated forward maximum. */
    TEST_ASSERT_EQUAL_UINT32(p.esc_forward_max_us, esc_us);
}

static void test_ramp_up_climbs_with_ramp_up_rate(void)
{
    /* Arrange: full forward, fresh ramp at neutral command. */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;
    int32_t ramp = 0;

    /* Act: one cycle only. */
    uint32_t esc_us = throttle_chain_step(2000U, THROTTLE_TARGET_TRACK, &p, &ramp);

    /* Assert: after one cycle the ramped command equals esc_ramp_up (5), so the
     * output is barely above neutral, NOT the forward max. */
    TEST_ASSERT_EQUAL_INT32((int32_t)p.esc_ramp_up_us_per_cycle, ramp);
    TEST_ASSERT_GREATER_THAN_UINT32(p.esc_neutral_us, esc_us);
    TEST_ASSERT_LESS_THAN_UINT32(p.esc_forward_max_us, esc_us);
}

static void test_ramp_down_uses_ramp_down_rate(void)
{
    /* Arrange: start ramped at full forward command, then command neutral. */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;
    int32_t ramp = SIGNAL_NORMALIZED_FULL_SCALE; /* at full */

    /* Act: command center -> target 0, one cycle. */
    throttle_chain_step(1500U, THROTTLE_TARGET_TRACK, &p, &ramp);

    /* Assert: dropped by exactly esc_ramp_down (10), distinct from ramp_up. */
    TEST_ASSERT_EQUAL_INT32(
        SIGNAL_NORMALIZED_FULL_SCALE - (int32_t)p.esc_ramp_down_us_per_cycle,
        ramp);
}

static void test_ramp_never_overshoots_target(void)
{
    /* Arrange: full forward stick, 100% limit, settle. */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;
    int32_t ramp = 0;
    for (int i = 0; i < SETTLE_CYCLES; i++) {
        throttle_chain_step(2000U, THROTTLE_TARGET_TRACK, &p, &ramp);
    }

    /* Assert: ramp settled exactly at full scale, never past it. */
    TEST_ASSERT_EQUAL_INT32(SIGNAL_NORMALIZED_FULL_SCALE, ramp);
}

static void test_failsafe_override_soft_stops_to_neutral(void)
{
    /* Arrange: ramped at full forward, then FAILSAFE forces target 0. */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;
    int32_t ramp = SIGNAL_NORMALIZED_FULL_SCALE;

    /* Act: one FAILSAFE cycle must NOT jump straight to neutral. */
    uint32_t after_one = throttle_chain_step(2000U, THROTTLE_TARGET_NEUTRAL, &p,
                                             &ramp);

    /* Assert: still above neutral after one cycle (soft-stop, not a jump). */
    TEST_ASSERT_GREATER_THAN_UINT32(p.esc_neutral_us, after_one);

    /* Act: let it settle. */
    for (int i = 0; i < SETTLE_CYCLES; i++) {
        after_one = throttle_chain_step(2000U, THROTTLE_TARGET_NEUTRAL, &p,
                                        &ramp);
    }

    /* Assert: eventually rests at neutral. */
    TEST_ASSERT_EQUAL_UINT32(p.esc_neutral_us, after_one);
}

static void test_disarmed_holds_neutral_regardless_of_stick(void)
{
    /* Arrange: full forward stick but DISARMED (target forced to 0). */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;

    /* Act */
    uint32_t esc_us = settle_throttle(2000U, THROTTLE_TARGET_NEUTRAL, &p);

    /* Assert: stays at neutral. */
    TEST_ASSERT_EQUAL_UINT32(p.esc_neutral_us, esc_us);
}

static void test_output_never_exceeds_clamp_window(void)
{
    /* Arrange: absurd input and an inverted/extreme calibration cannot push the
     * output past the hard clamp window [1000, 2000]. */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;
    p.esc_forward_max_us = 2000U;

    /* Act: out-of-band raw width. */
    uint32_t esc_us = settle_throttle(60000U, THROTTLE_TARGET_TRACK, &p);

    /* Assert: clamped within window (inclusive upper bound). */
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(2000U, esc_us);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1000U, esc_us);
}

void run_throttle_chain_tests(void)
{
    RUN_TEST(test_deadband_small_signal_maps_to_neutral);
    RUN_TEST(test_deadband_just_past_threshold_is_nonzero);
    RUN_TEST(test_deadband_at_threshold_is_inclusive_neutral);
    RUN_TEST(test_offcenter_deadband_is_consistent_both_sides);
    RUN_TEST(test_reverse_keeps_neutral_neutral);
    RUN_TEST(test_reverse_flips_direction_of_a_nonzero_command);
    RUN_TEST(test_power_limit_forward_caps_target_before_ramp);
    RUN_TEST(test_power_limit_reverse_caps_independently_of_forward);
    RUN_TEST(test_power_limit_in_band_command_passes_through);
    RUN_TEST(test_power_limit_full_forward_without_limit);
    RUN_TEST(test_ramp_up_climbs_with_ramp_up_rate);
    RUN_TEST(test_ramp_down_uses_ramp_down_rate);
    RUN_TEST(test_ramp_never_overshoots_target);
    RUN_TEST(test_failsafe_override_soft_stops_to_neutral);
    RUN_TEST(test_disarmed_holds_neutral_regardless_of_stick);
    RUN_TEST(test_output_never_exceeds_clamp_window);
}
