#include "loop_step.h"
#include "settings_validate.h"
#include "unity.h"

/* Capture-recency domain: 80 ticks/us. now_ticks and last_edge_ticks share it. */
#define TICKS_PER_US 80U
#define NOW_TICKS 1000000U

/* ESC neutral the throttle chain maps command 0 onto (default esc_neutral_us). */
#define ESC_NEUTRAL_US 1500U

static loop_validity_cfg make_cfg(void)
{
    rc_channel_cfg ch = {
        .width_min_us = 800U,
        .width_max_us = 2200U,
        .period_expected_us = 20000U,
        .period_tol_us = 8000U,
        .edge_timeout_us = 30000U,
    };
    loop_validity_cfg cfg = {.ch1 = ch, .ch2 = ch};
    return cfg;
}

/* A fresh, valid capture sample for a given pulse width. */
static rc_channel_sample sample_at(uint32_t width_us)
{
    rc_channel_sample s = {
        .width_us = width_us,
        .period_us = 20000U,
        .last_edge_ticks = NOW_TICKS - (1000U * TICKS_PER_US),
        .edge_seen = true,
    };
    return s;
}

/* An invalid sample: stale edge older than edge_timeout (40 ms ago). */
static rc_channel_sample stale_sample(uint32_t width_us)
{
    rc_channel_sample s = sample_at(width_us);
    s.last_edge_ticks = NOW_TICKS - (40000U * TICKS_PER_US);
    return s;
}

static loop_inputs make_inputs(uint32_t ch1_us, uint32_t ch2_us, bool arm)
{
    loop_inputs in = {
        .ch1 = sample_at(ch1_us),
        .ch2 = sample_at(ch2_us),
        .now_ticks = NOW_TICKS,
        .ui_arm_request = arm,
        .ui_disarm_request = false,
    };
    return in;
}

/* Drive the loop to ARMED: a few cycles of valid RC, neutral throttle, arm req.
 * The debounce starts latched-valid so one armable cycle is enough, but a couple
 * of cycles let any ramp/slew settle before the test perturbs the sticks. */
static void arm(loop_state *state, const loop_validity_cfg *cfg,
                const settings_params *params)
{
    loop_inputs in = make_inputs(1500U, 1500U, true);
    for (int i = 0; i < 3; i++) {
        loop_step(&in, cfg, params, state);
    }
}

static void test_disarmed_full_throttle_outputs_neutral_esc(void)
{
    /* Arrange: DISARMED (no arm request), throttle stick at max. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    loop_inputs in = make_inputs(1500U, 2000U, false);

    /* Act */
    loop_outputs out = loop_step(&in, &cfg, &params, &state);

    /* Assert: R7 gate keeps the ESC at neutral despite full throttle. */
    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
}

static void test_armed_throttle_tracks_with_ramp(void)
{
    /* Arrange: reach ARMED, then push throttle forward. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm(&state, &cfg, &params);
    loop_inputs in = make_inputs(1500U, 2000U, false);

    /* Act: one cycle of forward throttle while ARMED. */
    loop_outputs out = loop_step(&in, &cfg, &params, &state);

    /* Assert: armed and the ESC has ramped above neutral (toward forward). */
    TEST_ASSERT_EQUAL(SM_STATE_ARMED, out.telemetry.state);
    TEST_ASSERT_GREATER_THAN_UINT32(ESC_NEUTRAL_US, out.esc_us);
}

static void test_armed_servo_tracks_with_slew(void)
{
    /* Arrange: reach ARMED, then command steering toward one endpoint. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm(&state, &cfg, &params);

    uint32_t center = ((uint32_t)params.servo_min_us +
                       (uint32_t)params.servo_max_us) / 2U;
    loop_inputs in = make_inputs(2000U, 1500U, false);

    /* Act: drive the servo several cycles so the slew moves it off center. */
    loop_outputs out = {0};
    for (int i = 0; i < 5; i++) {
        out = loop_step(&in, &cfg, &params, &state);
    }

    /* Assert: servo has slewed away from center toward the commanded side. */
    TEST_ASSERT_EQUAL(SM_STATE_ARMED, out.telemetry.state);
    TEST_ASSERT_NOT_EQUAL(center, out.servo_us);
}

static void test_rc_invalid_failsafe_soft_stop_and_center(void)
{
    /* Arrange: ARMED at forward throttle and off-center steering, then RC dies
     * for enough consecutive frames to debounce-latch invalid. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm(&state, &cfg, &params);

    uint32_t center = ((uint32_t)params.servo_min_us +
                       (uint32_t)params.servo_max_us) / 2U;
    loop_inputs bad = {
        .ch1 = stale_sample(2000U),
        .ch2 = stale_sample(2000U),
        .now_ticks = NOW_TICKS,
        .ui_arm_request = false,
        .ui_disarm_request = false,
    };

    /* Act: run past the debounce threshold plus enough cycles for the ramp/slew
     * to reach neutral/center. */
    loop_outputs out = {0};
    for (int i = 0; i < 400; i++) {
        out = loop_step(&bad, &cfg, &params, &state);
    }

    /* Assert: latched FAILSAFE, ESC soft-stopped to neutral, servo centered. */
    TEST_ASSERT_EQUAL(SM_STATE_FAILSAFE, out.telemetry.state);
    TEST_ASSERT_FALSE(out.telemetry.rc_valid);
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
    TEST_ASSERT_EQUAL_UINT32(center, out.servo_us);
}

/* SI-3 hard clamp window for the ESC; the calibration output must stay inside
 * it on every step (proof the service mode never bypasses the clamp). */
#define ESC_CLAMP_MIN_US 1000U
#define ESC_CLAMP_MAX_US 2000U

/* Drive the loop from DISARMED into ESC_CALIBRATION with all entry conditions
 * (R15/SI-5) met, throttle held neutral. */
static void enter_calibration(loop_state *state, const loop_validity_cfg *cfg,
                              const settings_params *params)
{
    loop_inputs in = make_inputs(1500U, 1500U, false);
    in.ui_calib_request = true;
    in.ui_calib_confirm = true;
    loop_step(&in, cfg, params, state);
}

static void test_calibration_entry_requires_confirmation(void)
{
    /* Arrange: request calibration but withhold the confirmation. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    loop_inputs in = make_inputs(1500U, 1500U, false);
    in.ui_calib_request = true;
    in.ui_calib_confirm = false;

    /* Act */
    loop_outputs out = loop_step(&in, &cfg, &params, &state);

    /* Assert: no entry without confirmation; ESC stays at neutral. */
    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
}

static void test_calibration_steps_emit_constants_bypassing_throttle(void)
{
    /* Arrange: enter calibration, then hold full throttle on CH2 to prove the
     * throttle chain is bypassed (output follows the step, not the stick). */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    enter_calibration(&state, &cfg, &params);

    /* Step NEUTRAL: full throttle stick, but the ESC emits the 1500 constant. */
    loop_inputs hold = make_inputs(1500U, 2000U, false);
    loop_outputs out = loop_step(&hold, &cfg, &params, &state);
    TEST_ASSERT_EQUAL(SM_STATE_ESC_CALIBRATION, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(1500U, out.esc_us);

    /* NEXT -> FORWARD: 2000. */
    loop_inputs next = make_inputs(1500U, 2000U, false);
    next.calib_event = CALIB_EVENT_NEXT;
    out = loop_step(&next, &cfg, &params, &state);
    TEST_ASSERT_EQUAL(SM_STATE_ESC_CALIBRATION, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(2000U, out.esc_us);

    /* NEXT -> REVERSE: 1000. */
    out = loop_step(&next, &cfg, &params, &state);
    TEST_ASSERT_EQUAL(SM_STATE_ESC_CALIBRATION, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(1000U, out.esc_us);

    /* NEXT past REVERSE -> Done -> DISARMED + neutral. */
    out = loop_step(&next, &cfg, &params, &state);
    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
}

static void test_calibration_constants_lie_within_clamp_window(void)
{
    /* The calibration step constants are legal inputs: each emitted value sits
     * inside the ESC sanity window [1000, 2000]. NOTE: this alone does NOT prove
     * the clamp is on the path (the constants equal the window boundaries);
     * test_calib_clamp_esc_snaps_out_of_window_value_to_boundary provides that
     * oracle. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    enter_calibration(&state, &cfg, &params);

    loop_inputs next = make_inputs(1500U, 2000U, false);
    next.calib_event = CALIB_EVENT_NEXT;
    for (int i = 0; i < 3; i++) {
        loop_outputs out = loop_step(&next, &cfg, &params, &state);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(ESC_CLAMP_MIN_US, out.esc_us);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(ESC_CLAMP_MAX_US, out.esc_us);
    }
}

static void test_calib_clamp_esc_snaps_out_of_window_value_to_boundary(void)
{
    /* SI-3 ORACLE on the service-mode path. calib_clamp_esc is the SINGLE clamp
     * every calibration constant is routed through in run_calibration. Feed it a
     * calibration constant (2000) with a NARROWED window whose max is below it,
     * so a working clamp MUST snap the value to the boundary. If the clamp were
     * removed from the path (raw co.esc_us returned), the value would pass
     * through unchanged and these assertions would fail. The default window
     * [1000, 2000] cannot expose this because the constants equal its bounds. */
    PwmWindow narrow_high = {.min_us = 1000U, .max_us = 1500U};
    /* FORWARD constant 2000 exceeds max 1500 -> must be clamped to 1500. */
    TEST_ASSERT_EQUAL_UINT32(1500U, calib_clamp_esc(2000U, narrow_high));
    /* NEUTRAL constant 1500 is at the boundary -> unchanged. */
    TEST_ASSERT_EQUAL_UINT32(1500U, calib_clamp_esc(1500U, narrow_high));

    PwmWindow narrow_low = {.min_us = 1500U, .max_us = 2000U};
    /* REVERSE constant 1000 is below min 1500 -> must be clamped to 1500. */
    TEST_ASSERT_EQUAL_UINT32(1500U, calib_clamp_esc(1000U, narrow_low));
    /* FORWARD constant 2000 is inside -> unchanged. */
    TEST_ASSERT_EQUAL_UINT32(2000U, calib_clamp_esc(2000U, narrow_low));
}

static void test_calibration_timeout_returns_to_disarmed_neutral(void)
{
    /* Integration coverage of the calib_timeout abort on the loop path:
     * in->calib_timeout -> co.exit -> calib_exit_state -> DISARMED + neutral. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    enter_calibration(&state, &cfg, &params);

    loop_inputs timed_out = make_inputs(1500U, 2000U, false);
    timed_out.calib_timeout = true;

    loop_outputs out = loop_step(&timed_out, &cfg, &params, &state);

    /* Assert: idle timeout aborts the service mode to DISARMED, ESC parked. */
    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
}

static void test_calibration_entry_frame_ignores_event_starts_at_neutral(void)
{
    /* Entry-frame contract: when the SAME frame both enters calibration (UI
     * request+confirm) AND carries a calib_event (NEXT), the entry frame must
     * suppress the event so the sequence starts at NEUTRAL (1500), never
     * skipping to FORWARD (2000) on the first frame. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);

    /* Throttle held neutral (entry guard requires it); the perturbation under
     * test is the co-arriving calib_event=NEXT on the entry frame. */
    loop_inputs entry = make_inputs(1500U, 1500U, false);
    entry.ui_calib_request = true;
    entry.ui_calib_confirm = true;
    entry.calib_event = CALIB_EVENT_NEXT; /* co-arriving event on entry frame */

    loop_outputs out = loop_step(&entry, &cfg, &params, &state);

    /* Assert: entered calibration and held NEUTRAL (1500), event suppressed.
     * Without the entry-frame guard the NEXT would advance to FORWARD (2000). */
    TEST_ASSERT_EQUAL(SM_STATE_ESC_CALIBRATION, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(1500U, out.esc_us);
}

static void test_calibration_reentry_reinitialises_step_to_neutral(void)
{
    /* Re-usable service mode: after a full sequence exits to DISARMED, entering
     * calibration again must re-initialise the step to NEUTRAL (1500), not
     * resume at a stale step. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);

    /* First run: enter, then cancel back to DISARMED. */
    enter_calibration(&state, &cfg, &params);
    loop_inputs cancel = make_inputs(1500U, 1500U, false);
    cancel.calib_event = CALIB_EVENT_CANCEL;
    loop_step(&cancel, &cfg, &params, &state);

    /* Second run: re-enter and hold; must restart at NEUTRAL (1500). */
    enter_calibration(&state, &cfg, &params);
    loop_inputs hold = make_inputs(1500U, 2000U, false);
    loop_outputs out = loop_step(&hold, &cfg, &params, &state);

    TEST_ASSERT_EQUAL(SM_STATE_ESC_CALIBRATION, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(1500U, out.esc_us);
}

static void test_calibration_rc_loss_aborts_to_failsafe(void)
{
    /* Arrange: in calibration, then RC goes stale for enough frames to latch
     * invalid. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    enter_calibration(&state, &cfg, &params);

    loop_inputs bad = {
        .ch1 = stale_sample(1500U),
        .ch2 = stale_sample(1500U),
        .now_ticks = NOW_TICKS,
    };

    /* Act: run past the debounce threshold. */
    loop_outputs out = {0};
    for (int i = 0; i < 20; i++) {
        out = loop_step(&bad, &cfg, &params, &state);
    }

    /* Assert: RC loss aborts the service mode to FAILSAFE, ESC at neutral. */
    TEST_ASSERT_EQUAL(SM_STATE_FAILSAFE, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
}

static void test_calibration_cancel_returns_to_disarmed_neutral(void)
{
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    enter_calibration(&state, &cfg, &params);

    loop_inputs cancel = make_inputs(1500U, 1500U, false);
    cancel.calib_event = CALIB_EVENT_CANCEL;

    loop_outputs out = loop_step(&cancel, &cfg, &params, &state);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
}

static void test_pending_applies_only_in_disarmed(void)
{
    /* The apply gate (R17/SI-6): pending params may be applied ONLY in DISARMED.
     * In every other state the gate refuses, so active params are unchanged. */
    TEST_ASSERT_TRUE(loop_should_apply_pending(SM_STATE_DISARMED));
    TEST_ASSERT_FALSE(loop_should_apply_pending(SM_STATE_ARMED));
    TEST_ASSERT_FALSE(loop_should_apply_pending(SM_STATE_FAILSAFE));
    TEST_ASSERT_FALSE(loop_should_apply_pending(SM_STATE_ESC_CALIBRATION));
}

void run_loop_step_tests(void)
{
    RUN_TEST(test_disarmed_full_throttle_outputs_neutral_esc);
    RUN_TEST(test_armed_throttle_tracks_with_ramp);
    RUN_TEST(test_armed_servo_tracks_with_slew);
    RUN_TEST(test_rc_invalid_failsafe_soft_stop_and_center);
    RUN_TEST(test_calibration_entry_requires_confirmation);
    RUN_TEST(test_calibration_steps_emit_constants_bypassing_throttle);
    RUN_TEST(test_calibration_constants_lie_within_clamp_window);
    RUN_TEST(test_calib_clamp_esc_snaps_out_of_window_value_to_boundary);
    RUN_TEST(test_calibration_rc_loss_aborts_to_failsafe);
    RUN_TEST(test_calibration_cancel_returns_to_disarmed_neutral);
    RUN_TEST(test_calibration_timeout_returns_to_disarmed_neutral);
    RUN_TEST(test_calibration_entry_frame_ignores_event_starts_at_neutral);
    RUN_TEST(test_calibration_reentry_reinitialises_step_to_neutral);
    RUN_TEST(test_pending_applies_only_in_disarmed);
}
