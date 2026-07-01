#include "loop_step.h"

#include "esc_calibration.h"
#include "safety_clamp.h"
#include "signal_chain.h"

/* ESC output sanity window (SI-3). The service mode emits constants directly to
 * the ESC, so they pass through this same hard clamp as every other code path:
 * the calibration output never bypasses the clamp. */
#define ESC_WINDOW_MIN_US 1000U
#define ESC_WINDOW_MAX_US 2000U

/* Servo center used to seed the slew accumulator (no startup transient). */
static int32_t servo_center_us(const settings_params *params)
{
    return ((int32_t)params->servo_min_us + (int32_t)params->servo_max_us) / 2;
}

/* Convert the anti-plugging neutral dwell from milliseconds to whole control
 * cycles, rounding UP so the parked time is never shorter than configured.
 * 0 ms -> 0 frames (dwell disabled). Period comes from the single shared
 * CONTROL_LOOP_PERIOD_MS named constant (no hardcoded period here). */
static uint16_t reverse_dwell_frames(const settings_params *params)
{
    uint32_t ms = params->reverse_neutral_dwell_ms;
    if (ms == 0U) {
        return 0U;
    }
    uint32_t frames = (ms + CONTROL_LOOP_PERIOD_MS - 1U) / CONTROL_LOOP_PERIOD_MS;
    return (uint16_t)frames;
}

bool loop_should_apply_pending(sm_state state)
{
    return state == SM_STATE_DISARMED;
}

void loop_state_init(loop_state *state, const settings_params *params,
                     uint16_t debounce_threshold)
{
    state->state = SM_STATE_DISARMED;
    rc_debounce_init(&state->rc_debounce, debounce_threshold);
    state->throttle_ramp.value = 0;
    state->throttle_ramp.dwell_remaining = 0;
    state->servo_slew = servo_center_us(params);
    state->calib_step = CALIB_STEP_NEUTRAL;
    state->spot_lock.substate = SPOT_LOCK_OFF;
    state->spot_lock.ref_lat_e7 = 0;
    state->spot_lock.ref_lon_e7 = 0;
}

/* Debounced RC validity for this cycle: both control channels valid this frame,
 * fed through the latching debounce filter. */
static bool resolve_rc_valid(const loop_inputs *in, const loop_validity_cfg *cfg,
                             loop_state *state)
{
    bool ch1_ok = channel_valid(&in->ch1, in->now_ticks, &cfg->ch1);
    bool ch2_ok = channel_valid(&in->ch2, in->now_ticks, &cfg->ch2);
    bool frame_valid = rc_valid(ch1_ok, ch2_ok);
    return rc_debounce_update(&state->rc_debounce, frame_valid);
}

static sm_inputs build_sm_inputs(const loop_inputs *in,
                                 const settings_params *params, bool rc_is_valid,
                                 const loop_state *state)
{
    sm_inputs si = {
        .rc_valid = rc_is_valid,
        .throttle_neutral = throttle_is_neutral(in->ch2.width_us, params),
        .calib_in_progress = state->state == SM_STATE_ESC_CALIBRATION,
        .settings_apply_in_progress = false,
        .ui_arm_request = in->ui_arm_request,
        .ui_disarm_request = in->ui_disarm_request,
        .ui_calib_request = in->ui_calib_request,
        .ui_calib_confirm = in->ui_calib_confirm,
        .deploy_request = in->deploy_request,
        .stow_request = in->stow_request,
    };
    return si;
}

/* The SI-3 hard clamp window every ESC value is routed through. */
static PwmWindow esc_clamp_window(void)
{
    PwmWindow window = {.min_us = ESC_WINDOW_MIN_US, .max_us = ESC_WINDOW_MAX_US};
    return window;
}

uint32_t calib_clamp_esc(uint32_t esc_us, PwmWindow window)
{
    return clamp_pwm_us(esc_us, window);
}

/* Map the calibration sub-machine's exit to the next control state. */
static sm_state calib_exit_state(calib_exit exit)
{
    if (exit == CALIB_EXIT_TO_FAILSAFE) {
        return SM_STATE_FAILSAFE;
    }
    return SM_STATE_DISARMED; /* done / cancel / timeout */
}

/* Detect the calibration entry frame and reset the step to NEUTRAL. The entry
 * frame ignores any operator event so the sequence always starts at NEUTRAL
 * (1500 us) and cannot skip the first step (entry-frame contract). Pure state
 * mutation: no output is produced here. */
static void reset_calib_on_entry(loop_state *state, const sm_outputs *sm)
{
    bool entering_calib = state->state != SM_STATE_ESC_CALIBRATION &&
                          sm->state == SM_STATE_ESC_CALIBRATION;
    if (!entering_calib) {
        return;
    }
    state->calib_step = CALIB_STEP_NEUTRAL;
}

/* Whether this cycle is the calibration entry frame (just crossed into calib).
 * On the entry frame the operator event is suppressed so the first step is
 * always NEUTRAL regardless of a co-arriving NEXT/CANCEL. */
static bool is_calib_entry_frame(const loop_state *state, const sm_outputs *sm)
{
    return state->state != SM_STATE_ESC_CALIBRATION &&
           sm->state == SM_STATE_ESC_CALIBRATION;
}

/* Compute one calibration cycle: step the sub-machine, park the throttle ramp,
 * and route the constant through the SI-3 hard clamp. Pure with respect to the
 * next state, which it reports via *exit (the caller maps it to a state). */
static uint32_t compute_calib_esc(const loop_inputs *in, bool rc_is_valid,
                                  bool entry_frame, loop_state *state,
                                  calib_exit *exit)
{
    calib_inputs ci = {
        .step = state->calib_step,
        .event = entry_frame ? CALIB_EVENT_NONE : in->calib_event,
        .rc_valid = rc_is_valid,
        .timeout = entry_frame ? false : in->calib_timeout,
    };
    calib_outputs co = calib_step_next(&ci);
    state->calib_step = co.step;
    state->throttle_ramp.value = 0;
    state->throttle_ramp.dwell_remaining = 0;
    *exit = co.exit;
    return calib_clamp_esc(co.esc_us, esc_clamp_window());
}

/* Drive the ESC calibration sequence for one cycle, resolving the next control
 * state from the abort rules. The ESC constant bypasses the throttle chain but
 * still passes through the hard clamp; the ramp is parked so re-entry to
 * DISARMED has no transient. */
static uint32_t run_calibration(const loop_inputs *in, bool rc_is_valid,
                                bool entry_frame, loop_state *state,
                                sm_state *next_state)
{
    calib_exit exit = CALIB_EXIT_NONE;
    uint32_t esc_us = compute_calib_esc(in, rc_is_valid, entry_frame, state,
                                        &exit);
    if (exit != CALIB_EXIT_NONE) {
        *next_state = calib_exit_state(exit);
    }
    return esc_us;
}

/* Resolve the ESC output and the next state, branching on calibration. In
 * ESC_CALIBRATION the throttle chain is bypassed: the ESC is driven by the
 * calibration sequence constants (still through the SI-3 hard clamp), and the
 * abort rules may override the next state. Otherwise the throttle chain runs. */
static uint32_t resolve_esc(const loop_inputs *in, const settings_params *params,
                            bool rc_is_valid, const sm_outputs *sm,
                            throttle_target_mode throttle_mode,
                            int32_t spot_lock_throttle, loop_state *state,
                            sm_state *next_state)
{
    if (sm->state == SM_STATE_ESC_CALIBRATION) {
        bool entry_frame = is_calib_entry_frame(state, sm);
        reset_calib_on_entry(state, sm);
        return run_calibration(in, rc_is_valid, entry_frame, state, next_state);
    }
    return throttle_chain_step(in->ch2.width_us, throttle_mode, spot_lock_throttle,
                               params, reverse_dwell_frames(params),
                               &state->throttle_ramp);
}

/* Percent thrust cap -> normalized command full-scale (100% == full-scale). */
#define SPOT_LOCK_PERCENT_FULL 100

/* Both control sticks within their neutral bands this cycle (R3/R4): the shared
 * neutrality domain for spot-lock entry and abort. */
static bool sticks_within_neutral(const loop_inputs *in,
                                  const settings_params *params)
{
    return throttle_is_neutral(in->ch2.width_us, params) &&
           steer_is_neutral(in->ch1.width_us, params);
}

/* Map the per-cycle loop inputs onto the pure spot_lock inputs. armed is always
 * true here: this is only built inside the ARMED branch, where failsafe
 * precedence is already guaranteed by the caller. */
static spot_lock_inputs build_spot_lock_inputs(const loop_inputs *in,
                                               const settings_params *params)
{
    spot_lock_inputs sli = {
        .armed = true,
        .ch3_on = in->spot_lock_switch_on,
        .ch3_edge_on = in->spot_lock_switch_edge_on,
        .sticks_neutral = sticks_within_neutral(in, params),
        .gps_fresh = in->gps_fresh,
        .gps_has_fix = in->gps_has_fix,
        .imu_ok = in->imu_ok,
        .lat_e7 = in->gps_lat_e7,
        .lon_e7 = in->gps_lon_e7,
        .heading_deg10 = in->imu_heading_deg10,
    };
    return sli;
}

/* Map the active settings onto the pure regulator params (percent cap -> the
 * normalized command full-scale the regulator saturates to). */
static spot_lock_params build_spot_lock_params(const settings_params *params)
{
    spot_lock_params slp = {
        .deadband_m = params->spot_lock_deadband_m,
        .max_throttle_norm =
            (uint16_t)((int32_t)SPOT_LOCK_CMD_FULL_SCALE *
                       (int32_t)params->spot_lock_max_throttle_pct /
                       SPOT_LOCK_PERCENT_FULL),
        .throttle_gain_per_m = params->spot_lock_throttle_gain,
        .servo_gain_per_deg = params->spot_lock_servo_gain,
    };
    return slp;
}

/* Run the spot-lock regulator for one cycle, but ONLY while the resolved control
 * state is ARMED. Outside ARMED (DISARMED/FAILSAFE/calibration/deploy) spot-lock
 * is forced OFF and yields unconditionally -- the single mechanism that makes
 * failsafe always win over spot-lock. */
static spot_lock_outputs resolve_spot_lock(const loop_inputs *in,
                                           const settings_params *params,
                                           sm_state resolved_state,
                                           spot_lock_state *st)
{
    if (resolved_state != SM_STATE_ARMED) {
        st->substate = SPOT_LOCK_OFF;
        spot_lock_outputs off = {.substate = SPOT_LOCK_OFF};
        return off;
    }
    spot_lock_inputs sli = build_spot_lock_inputs(in, params);
    spot_lock_params slp = build_spot_lock_params(params);
    return spot_lock_step(&sli, &slp, st);
}

/* Spot-lock drives the actuators only while ACTIVE (holding) or PAUSED (sensor
 * loss -> neutral+center via the chain). OFF leaves manual stick tracking. */
static bool spot_lock_drives(spot_lock_substate s)
{
    return s == SPOT_LOCK_ACTIVE || s == SPOT_LOCK_PAUSED;
}

loop_outputs loop_step(const loop_inputs *in, const loop_validity_cfg *cfg,
                       const settings_params *params, loop_state *state)
{
    bool rc_is_valid = resolve_rc_valid(in, cfg, state);

    sm_inputs si = build_sm_inputs(in, params, rc_is_valid, state);
    sm_outputs sm = sm_step(state->state, &si);

    /* Spot-lock override: applied AFTER sm_step and ONLY in ARMED, so a FAILSAFE
     * (or any non-ARMED) result bypasses it entirely and failsafe always wins. */
    spot_lock_outputs sl = resolve_spot_lock(in, params, sm.state,
                                             &state->spot_lock);
    bool drive_spot_lock = spot_lock_drives(sl.substate);
    throttle_target_mode throttle_mode =
        drive_spot_lock ? THROTTLE_TARGET_SPOT_LOCK : sm.throttle_target;
    servo_target_mode servo_mode =
        drive_spot_lock ? SERVO_TARGET_SPOT_LOCK : sm.servo_target;

    sm_state next_state = sm.state;
    uint32_t esc_us = resolve_esc(in, params, rc_is_valid, &sm, throttle_mode,
                                  sl.throttle_cmd, state, &next_state);
    uint32_t servo_us = servo_chain_step(in->ch1.width_us, servo_mode,
                                         sl.servo_cmd, params, &state->servo_slew);
    state->state = next_state;

    loop_outputs out = {
        .esc_us = esc_us,
        .servo_us = servo_us,
        .telemetry = {
            .state = next_state,
            .rc_valid = rc_is_valid,
            .esc_us = esc_us,
            .servo_us = servo_us,
            .arm_reason = sm_arm_block_reason(&si),
            .spot_lock_substate = (uint8_t)sl.substate,
            .spot_lock_err_m = sl.err_m,
            .spot_lock_bearing_deg10 = sl.bearing_deg10,
        },
    };
    return out;
}
