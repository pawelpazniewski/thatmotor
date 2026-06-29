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
        .period_min_us = 2000U,
        .period_max_us = 30000U,
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

/* Build a spot-lock-ready input set: neutral sticks (so manual would be
 * neutral/center), valid fresh RC, and a fresh GPS fix + IMU heading at the
 * given position. ch3 level/edge control entry and abort. */
static loop_inputs spot_lock_inputs_at(int32_t lat_e7, int32_t lon_e7,
                                       uint16_t heading_deg10, bool ch3_on,
                                       bool ch3_edge_on)
{
    loop_inputs in = make_inputs(1500U, 1500U, false);
    in.spot_lock_switch_on = ch3_on;
    in.spot_lock_switch_edge_on = ch3_edge_on;
    in.gps_fresh = true;
    in.gps_has_fix = true;
    in.imu_ok = true;
    in.gps_lat_e7 = lat_e7;
    in.gps_lon_e7 = lon_e7;
    in.imu_heading_deg10 = heading_deg10;
    return in;
}

/* Reach ARMED, then enter spot-lock at the origin (CH3 rising edge, neutral
 * sticks, fresh fix). Returns with substate ACTIVE and the target snapshot at
 * (0,0). Heading of the entry frame is irrelevant (distance 0 -> idle). */
static void arm_and_enter_spot_lock(loop_state *state,
                                    const loop_validity_cfg *cfg,
                                    const settings_params *params)
{
    arm(state, cfg, params);
    loop_inputs enter = spot_lock_inputs_at(0, 0, 0, true, true);
    loop_outputs out = loop_step(&enter, cfg, params, state);
    TEST_ASSERT_EQUAL_UINT8(SPOT_LOCK_ACTIVE, out.telemetry.spot_lock_substate);
}

/* Bow heading that points due SOUTH (deg*10): the bearing from a point NORTH of
 * the target back to the target. Aligning heading with it engages forward thrust. */
#define HEADING_SOUTH_DEG10 1800U
/* A drift ~22 m north of the target (0.0002 deg lat); well outside the deadband. */
#define DRIFT_NORTH_LAT_E7 2000

static void test_spot_lock_holds_with_computed_throttle(void)
{
    /* Arrange: armed + spot-lock active, target at origin. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm_and_enter_spot_lock(&state, &cfg, &params);

    /* Act: drifted ~22 m north, bow already pointing south (toward target), so
     * the regulator adds forward thrust. CH3 held on, sticks neutral. */
    loop_inputs hold =
        spot_lock_inputs_at(DRIFT_NORTH_LAT_E7, 0, HEADING_SOUTH_DEG10, true, false);
    loop_outputs out = {0};
    for (int i = 0; i < 400; i++) {
        out = loop_step(&hold, &cfg, &params, &state);
    }

    /* Assert: still ARMED + ACTIVE, ESC computed ABOVE neutral (manual at a
     * neutral stick would be neutral), telemetry reports the error + bearing. */
    TEST_ASSERT_EQUAL(SM_STATE_ARMED, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT8(SPOT_LOCK_ACTIVE, out.telemetry.spot_lock_substate);
    TEST_ASSERT_GREATER_THAN_UINT32(ESC_NEUTRAL_US, out.esc_us);
    TEST_ASSERT_TRUE(out.telemetry.spot_lock_err_m > 0U);
    TEST_ASSERT_EQUAL_UINT16(HEADING_SOUTH_DEG10,
                             out.telemetry.spot_lock_bearing_deg10);
}

static void test_failsafe_beats_spot_lock(void)
{
    /* Arrange: spot-lock active and drifted (it WOULD command forward thrust). */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm_and_enter_spot_lock(&state, &cfg, &params);

    uint32_t center = ((uint32_t)params.servo_min_us +
                       (uint32_t)params.servo_max_us) / 2U;

    /* Act: RC dies (stale edges) while the GPS still shows a drifted, fresh fix
     * with the bow aligned for thrust. sm_step latches FAILSAFE; spot-lock must
     * NOT run outside ARMED. */
    loop_inputs bad =
        spot_lock_inputs_at(DRIFT_NORTH_LAT_E7, 0, HEADING_SOUTH_DEG10, true, false);
    bad.ch1 = stale_sample(1500U);
    bad.ch2 = stale_sample(1500U);
    loop_outputs out = {0};
    for (int i = 0; i < 400; i++) {
        out = loop_step(&bad, &cfg, &params, &state);
    }

    /* Assert: FAILSAFE wins -> ESC neutral + servo center, spot-lock forced OFF.
     * If the override ran outside ARMED the ESC would be forward (FAIL). */
    TEST_ASSERT_EQUAL(SM_STATE_FAILSAFE, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
    TEST_ASSERT_EQUAL_UINT32(center, out.servo_us);
    TEST_ASSERT_EQUAL_UINT8(SPOT_LOCK_OFF, out.telemetry.spot_lock_substate);
}

static void test_spot_lock_ch3_off_returns_to_manual(void)
{
    /* Arrange: spot-lock active and drifted (commanding forward thrust). */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm_and_enter_spot_lock(&state, &cfg, &params);
    loop_inputs hold =
        spot_lock_inputs_at(DRIFT_NORTH_LAT_E7, 0, HEADING_SOUTH_DEG10, true, false);
    for (int i = 0; i < 400; i++) {
        loop_step(&hold, &cfg, &params, &state);
    }

    /* Act: CH3 switched OFF -> manual must resume within ONE cycle. */
    loop_inputs off =
        spot_lock_inputs_at(DRIFT_NORTH_LAT_E7, 0, HEADING_SOUTH_DEG10, false, false);
    loop_outputs out = loop_step(&off, &cfg, &params, &state);

    /* Assert: spot-lock OFF after a single cycle (manual tracking resumed). */
    TEST_ASSERT_EQUAL_UINT8(SPOT_LOCK_OFF, out.telemetry.spot_lock_substate);
}

static void test_spot_lock_stick_aborts_immediately(void)
{
    /* Arrange: spot-lock active and drifted (forward thrust engaged). */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm_and_enter_spot_lock(&state, &cfg, &params);
    loop_inputs hold =
        spot_lock_inputs_at(DRIFT_NORTH_LAT_E7, 0, HEADING_SOUTH_DEG10, true, false);
    loop_outputs out = {0};
    for (int i = 0; i < 400; i++) {
        out = loop_step(&hold, &cfg, &params, &state);
    }
    TEST_ASSERT_GREATER_THAN_UINT32(ESC_NEUTRAL_US, out.esc_us); /* was forward */

    /* Act: operator nudges the STEERING stick off-center (throttle still
     * neutral); CH3 stays on. The stick deflection aborts spot-lock at once. */
    loop_inputs nudge =
        spot_lock_inputs_at(DRIFT_NORTH_LAT_E7, 0, HEADING_SOUTH_DEG10, true, false);
    nudge.ch1 = sample_at(2000U); /* hard steer */
    out = loop_step(&nudge, &cfg, &params, &state);
    TEST_ASSERT_EQUAL_UINT8(SPOT_LOCK_OFF, out.telemetry.spot_lock_substate);

    /* Assert: manual resumes -> throttle eases to neutral (stick neutral) and the
     * servo follows the steering stick off center. */
    for (int i = 0; i < 400; i++) {
        out = loop_step(&nudge, &cfg, &params, &state);
    }
    uint32_t center = ((uint32_t)params.servo_min_us +
                       (uint32_t)params.servo_max_us) / 2U;
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
    TEST_ASSERT_NOT_EQUAL(center, out.servo_us);
}

static void test_spot_lock_pauses_on_gps_loss(void)
{
    /* Arrange: spot-lock active and drifted. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm_and_enter_spot_lock(&state, &cfg, &params);
    loop_inputs hold =
        spot_lock_inputs_at(DRIFT_NORTH_LAT_E7, 0, HEADING_SOUTH_DEG10, true, false);
    for (int i = 0; i < 400; i++) {
        loop_step(&hold, &cfg, &params, &state);
    }

    uint32_t center = ((uint32_t)params.servo_min_us +
                       (uint32_t)params.servo_max_us) / 2U;

    /* Act: GPS freshness lost (still ARMED, CH3 on, sticks neutral). */
    loop_inputs paused =
        spot_lock_inputs_at(DRIFT_NORTH_LAT_E7, 0, HEADING_SOUTH_DEG10, true, false);
    paused.gps_fresh = false;
    loop_outputs out = {0};
    for (int i = 0; i < 400; i++) {
        out = loop_step(&paused, &cfg, &params, &state);
    }

    /* Assert: PAUSED (NOT OFF, NOT failsafe) -> ESC neutral + servo center, state
     * stays ARMED so recovering the fix resumes the hold. */
    TEST_ASSERT_EQUAL(SM_STATE_ARMED, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT8(SPOT_LOCK_PAUSED, out.telemetry.spot_lock_substate);
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
    TEST_ASSERT_EQUAL_UINT32(center, out.servo_us);
}

static void test_spot_lock_output_passes_hard_clamp(void)
{
    /* SI-3 on the integrated spot-lock path: an out-of-band forward endpoint plus
     * a 100% thrust cap would map a saturated command to 3000 us; the hard clamp
     * MUST snap the ESC to the 2000 us window ceiling. */
    settings_params params;
    settings_load_defaults(&params);
    params.spot_lock_max_throttle_pct = 100U;
    params.esc_forward_max_us = 3000U; /* out of the [1000,2000] window */
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm_and_enter_spot_lock(&state, &cfg, &params);

    /* Act: large drift + bow aligned -> command saturates to the cap. */
    loop_inputs hold =
        spot_lock_inputs_at(50000, 0, HEADING_SOUTH_DEG10, true, false);
    loop_outputs out = {0};
    for (int i = 0; i < 800; i++) {
        out = loop_step(&hold, &cfg, &params, &state);
    }

    /* Assert: clamped to the hard ceiling, never above it. */
    TEST_ASSERT_EQUAL_UINT8(SPOT_LOCK_ACTIVE, out.telemetry.spot_lock_substate);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(2000U, out.esc_us);
    TEST_ASSERT_EQUAL_UINT32(2000U, out.esc_us);
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
    RUN_TEST(test_spot_lock_holds_with_computed_throttle);
    RUN_TEST(test_failsafe_beats_spot_lock);
    RUN_TEST(test_spot_lock_ch3_off_returns_to_manual);
    RUN_TEST(test_spot_lock_stick_aborts_immediately);
    RUN_TEST(test_spot_lock_pauses_on_gps_loss);
    RUN_TEST(test_spot_lock_output_passes_hard_clamp);
}
