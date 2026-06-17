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

/* Dwell disabled for the legacy single-direction tests (no flip exercised). */
#define NO_DWELL_FRAMES 0U

/* Run the throttle chain repeatedly with a fixed input so the ramp settles,
 * then return the final ESC pulse width. Starts from neutral with no dwell. */
static uint32_t settle_throttle(uint32_t raw_us, throttle_target_mode mode,
                                const settings_params *p)
{
    throttle_ramp_state st = {.value = 0, .dwell_remaining = 0};
    uint32_t esc_us = 0;
    for (int i = 0; i < SETTLE_CYCLES; i++) {
        esc_us = throttle_chain_step(raw_us, mode, p, NO_DWELL_FRAMES, &st);
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
    throttle_ramp_state st = {.value = 0, .dwell_remaining = 0};

    /* Act: one cycle only. */
    uint32_t esc_us =
        throttle_chain_step(2000U, THROTTLE_TARGET_TRACK, &p, NO_DWELL_FRAMES,
                            &st);

    /* Assert: after one cycle the ramped command equals esc_ramp_up (5), so the
     * output is barely above neutral, NOT the forward max. */
    TEST_ASSERT_EQUAL_INT32((int32_t)p.esc_ramp_up_us_per_cycle, st.value);
    TEST_ASSERT_GREATER_THAN_UINT32(p.esc_neutral_us, esc_us);
    TEST_ASSERT_LESS_THAN_UINT32(p.esc_forward_max_us, esc_us);
}

static void test_ramp_down_uses_ramp_down_rate(void)
{
    /* Arrange: start ramped at full forward command, then command neutral. */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;
    throttle_ramp_state st = {.value = SIGNAL_NORMALIZED_FULL_SCALE,
                              .dwell_remaining = 0}; /* at full */

    /* Act: command center -> target 0, one cycle. */
    throttle_chain_step(1500U, THROTTLE_TARGET_TRACK, &p, NO_DWELL_FRAMES, &st);

    /* Assert: dropped by exactly esc_ramp_down (10), distinct from ramp_up. */
    TEST_ASSERT_EQUAL_INT32(
        SIGNAL_NORMALIZED_FULL_SCALE - (int32_t)p.esc_ramp_down_us_per_cycle,
        st.value);
}

static void test_ramp_never_overshoots_target(void)
{
    /* Arrange: full forward stick, 100% limit, settle. */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;
    throttle_ramp_state st = {.value = 0, .dwell_remaining = 0};
    for (int i = 0; i < SETTLE_CYCLES; i++) {
        throttle_chain_step(2000U, THROTTLE_TARGET_TRACK, &p, NO_DWELL_FRAMES,
                            &st);
    }

    /* Assert: ramp settled exactly at full scale, never past it. */
    TEST_ASSERT_EQUAL_INT32(SIGNAL_NORMALIZED_FULL_SCALE, st.value);
}

static void test_failsafe_override_soft_stops_to_neutral(void)
{
    /* Arrange: ramped at full forward, then FAILSAFE forces target 0. */
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;
    throttle_ramp_state st = {.value = SIGNAL_NORMALIZED_FULL_SCALE,
                              .dwell_remaining = 0};

    /* Act: one FAILSAFE cycle must NOT jump straight to neutral. */
    uint32_t after_one =
        throttle_chain_step(2000U, THROTTLE_TARGET_NEUTRAL, &p, NO_DWELL_FRAMES,
                            &st);

    /* Assert: still above neutral after one cycle (soft-stop, not a jump). */
    TEST_ASSERT_GREATER_THAN_UINT32(p.esc_neutral_us, after_one);

    /* Act: let it settle. */
    for (int i = 0; i < SETTLE_CYCLES; i++) {
        after_one = throttle_chain_step(2000U, THROTTLE_TARGET_NEUTRAL, &p,
                                        NO_DWELL_FRAMES, &st);
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

/* Params with both directions wide open and a reverse stick mapped 1:1, used by
 * the direction-manager tests so the reverse target reaches full scale. */
static settings_params open_both_directions(void)
{
    settings_params p = defaults_params();
    p.max_throttle_fwd_pct = 100U;
    p.max_throttle_rev_pct = 100U;
    return p;
}

static void test_reversing_never_crosses_neutral_until_dwell_elapses(void)
{
    /* Arrange: already spun up full FORWARD (positive ramp value), operator
     * slams the stick to full REVERSE. Dwell of 5 frames. ANTI-PLUGGING ORACLE:
     * the ESC output must stay at/above neutral for the whole ramp-down AND the
     * whole dwell, and only then drop below neutral. */
    const uint16_t dwell_frames = 5U;
    settings_params p = open_both_directions();
    throttle_ramp_state st = {.value = SIGNAL_NORMALIZED_FULL_SCALE,
                              .dwell_remaining = 0};

    /* Act + Assert: while ramping down to neutral the value is positive, so the
     * output is strictly above neutral (never crosses to reverse). */
    uint32_t esc_us = 0;
    int rampdown_cycles = 0;
    do {
        esc_us = throttle_chain_step(1000U, THROTTLE_TARGET_TRACK, &p,
                                     dwell_frames, &st);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(p.esc_neutral_us, esc_us);
        rampdown_cycles++;
    } while (st.value != 0);
    /* full scale 1000 / ramp_down 10 = 100 cycles to reach neutral. */
    TEST_ASSERT_EQUAL_INT(100, rampdown_cycles);

    /* Act + Assert: the next `dwell_frames` cycles hold exactly neutral; the
     * output must NOT have crossed below neutral yet. */
    for (uint16_t i = 0; i < dwell_frames; i++) {
        esc_us = throttle_chain_step(1000U, THROTTLE_TARGET_TRACK, &p,
                                     dwell_frames, &st);
        TEST_ASSERT_EQUAL_UINT32(p.esc_neutral_us, esc_us);
        TEST_ASSERT_EQUAL_INT32(0, st.value);
    }

    /* Act + Assert: only AFTER the dwell does the output finally drop below
     * neutral (reverse side). Removing the dwell logic would make this fire on
     * the first post-rampdown cycle, so the held-neutral asserts above fail. */
    esc_us = throttle_chain_step(1000U, THROTTLE_TARGET_TRACK, &p, dwell_frames,
                                 &st);
    TEST_ASSERT_LESS_THAN_UINT32(p.esc_neutral_us, esc_us);
    TEST_ASSERT_LESS_THAN_INT32(0, st.value);
}

static void test_dwell_holds_neutral_for_exactly_the_requested_frames(void)
{
    /* Arrange: ramp value sitting one ramp-down step above neutral so the flip
     * reaches neutral on the very first cycle, isolating the dwell count. */
    const uint16_t dwell_frames = 7U;
    settings_params p = open_both_directions();
    throttle_ramp_state st = {
        .value = (int32_t)p.esc_ramp_down_us_per_cycle, /* one step from 0 */
        .dwell_remaining = 0};

    /* Act: first cycle reaches neutral and arms the dwell. */
    throttle_chain_step(1000U, THROTTLE_TARGET_TRACK, &p, dwell_frames, &st);
    TEST_ASSERT_EQUAL_INT32(0, st.value);
    TEST_ASSERT_EQUAL_UINT16(dwell_frames, st.dwell_remaining);

    /* Act + Assert: exactly `dwell_frames` cycles hold neutral. */
    for (uint16_t i = 0; i < dwell_frames; i++) {
        throttle_chain_step(1000U, THROTTLE_TARGET_TRACK, &p, dwell_frames, &st);
        TEST_ASSERT_EQUAL_INT32(0, st.value);
    }

    /* Assert: the next cycle finally moves into reverse. */
    throttle_chain_step(1000U, THROTTLE_TARGET_TRACK, &p, dwell_frames, &st);
    TEST_ASSERT_LESS_THAN_INT32(0, st.value);
}

static void test_start_from_neutral_ramps_immediately_without_dwell(void)
{
    /* Arrange: motor stopped (value 0), operator commands full reverse. Starting
     * from neutral is NOT a flip, so there must be no forced dwell. */
    const uint16_t dwell_frames = 50U;
    settings_params p = open_both_directions();
    throttle_ramp_state st = {.value = 0, .dwell_remaining = 0};

    /* Act: one cycle. */
    throttle_chain_step(1000U, THROTTLE_TARGET_TRACK, &p, dwell_frames, &st);

    /* Assert: ramped straight into reverse with no forced dwell. Spinning UP away
     * from neutral uses the gentle accel rate (rate_up) in EITHER direction
     * (magnitude-aware ramp), so reverse spin-up mirrors forward spin-up. */
    TEST_ASSERT_EQUAL_INT32(-(int32_t)p.esc_ramp_up_us_per_cycle, st.value);
    TEST_ASSERT_EQUAL_UINT16(0, st.dwell_remaining);
}

static void test_deadband_signal_is_not_treated_as_reversing(void)
{
    /* Arrange: spun up forward, then a tiny stick wobble that lands inside the
     * deadband (target 0). target == neutral must NOT count as a reverse flip:
     * the ramp eases down normally and no dwell is armed. */
    const uint16_t dwell_frames = 30U;
    settings_params p = open_both_directions();
    throttle_ramp_state st = {.value = SIGNAL_NORMALIZED_FULL_SCALE,
                              .dwell_remaining = 0};

    /* Act: 1580 us is exactly at the deadband edge -> target 0. */
    throttle_chain_step(1580U, THROTTLE_TARGET_TRACK, &p, dwell_frames, &st);

    /* Assert: eased down by one ramp-down step, still positive, no dwell armed. */
    TEST_ASSERT_EQUAL_INT32(
        SIGNAL_NORMALIZED_FULL_SCALE - (int32_t)p.esc_ramp_down_us_per_cycle,
        st.value);
    TEST_ASSERT_EQUAL_UINT16(0, st.dwell_remaining);
}

static void test_failsafe_soft_stops_without_blocking_dwell(void)
{
    /* Arrange: spun up forward, FAILSAFE forces target neutral. The soft-stop
     * must just coast to neutral and stay there -- no anti-plugging dwell, since
     * neutral is not the opposite direction. */
    const uint16_t dwell_frames = 25U;
    settings_params p = open_both_directions();
    throttle_ramp_state st = {.value = SIGNAL_NORMALIZED_FULL_SCALE,
                              .dwell_remaining = 0};

    /* Act: settle under FAILSAFE. */
    uint32_t esc_us = 0;
    for (int i = 0; i < SETTLE_CYCLES; i++) {
        esc_us = throttle_chain_step(2000U, THROTTLE_TARGET_NEUTRAL, &p,
                                     dwell_frames, &st);
    }

    /* Assert: rests exactly at neutral, no dwell armed (no spurious blocking). */
    TEST_ASSERT_EQUAL_UINT32(p.esc_neutral_us, esc_us);
    TEST_ASSERT_EQUAL_INT32(0, st.value);
    TEST_ASSERT_EQUAL_UINT16(0, st.dwell_remaining);
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
    RUN_TEST(test_reversing_never_crosses_neutral_until_dwell_elapses);
    RUN_TEST(test_dwell_holds_neutral_for_exactly_the_requested_frames);
    RUN_TEST(test_start_from_neutral_ramps_immediately_without_dwell);
    RUN_TEST(test_deadband_signal_is_not_treated_as_reversing);
    RUN_TEST(test_failsafe_soft_stops_without_blocking_dwell);
}
