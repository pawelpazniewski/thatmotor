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
    state->cruise_active = false;
    state->cruise_command_pct = 0;
}

static void reset_cruise(loop_state *state)
{
    state->cruise_active = false;
    state->cruise_command_pct = 0;
}

/* Cruise is a future feature, but safety is enforced now: any transition away
 * from ARMED clears a latent cruise hold so it cannot survive disarm, failsafe,
 * calibration, deploy, or boot/re-init and later re-arm with hidden power. */
static void apply_cruise_lifecycle(loop_state *state, sm_state next_state)
{
    if (next_state != SM_STATE_ARMED) {
        reset_cruise(state);
    }
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
        .cruise_active = state->cruise_active,
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
                            loop_state *state, sm_state *next_state)
{
    if (sm->state == SM_STATE_ESC_CALIBRATION) {
        bool entry_frame = is_calib_entry_frame(state, sm);
        reset_calib_on_entry(state, sm);
        return run_calibration(in, rc_is_valid, entry_frame, state, next_state);
    }
    return throttle_chain_step(in->ch2.width_us, sm->throttle_target, params,
                               reverse_dwell_frames(params),
                               &state->throttle_ramp);
}

loop_outputs loop_step(const loop_inputs *in, const loop_validity_cfg *cfg,
                       const settings_params *params, loop_state *state)
{
    bool rc_is_valid = resolve_rc_valid(in, cfg, state);

    sm_inputs si = build_sm_inputs(in, params, rc_is_valid, state);
    sm_outputs sm = sm_step(state->state, &si);

    sm_state next_state = sm.state;
    uint32_t esc_us = resolve_esc(in, params, rc_is_valid, &sm, state,
                                  &next_state);
    uint32_t servo_us = servo_chain_step(in->ch1.width_us, sm.servo_target,
                                         params, &state->servo_slew);
    apply_cruise_lifecycle(state, next_state);
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
            .cruise_active = state->cruise_active,
            .cruise_command_pct = state->cruise_command_pct,
        },
    };
    return out;
}
