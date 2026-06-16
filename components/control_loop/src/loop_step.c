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

bool loop_should_apply_pending(sm_state state)
{
    return state == SM_STATE_DISARMED;
}

void loop_state_init(loop_state *state, const settings_params *params,
                     uint16_t debounce_threshold)
{
    state->state = SM_STATE_DISARMED;
    rc_debounce_init(&state->rc_debounce, debounce_threshold);
    state->throttle_ramp = 0;
    state->servo_slew = servo_center_us(params);
    state->calib_step = CALIB_STEP_NEUTRAL;
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
    };
    return si;
}

/* Clamp every ESC value through the SI-3 hard clamp before it leaves the loop. */
static uint32_t clamp_esc(uint32_t esc_us)
{
    PwmWindow window = {.min_us = ESC_WINDOW_MIN_US, .max_us = ESC_WINDOW_MAX_US};
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

/* Drive the ESC calibration sequence for one cycle. Overrides the ESC output
 * with the step constant (bypassing the throttle chain) but still through the
 * hard clamp, and resolves the next control state from the abort rules. The
 * throttle ramp is parked at neutral so re-entry to DISARMED has no transient. */
static uint32_t run_calibration(const loop_inputs *in, bool rc_is_valid,
                                loop_state *state, sm_state *next_state)
{
    calib_inputs ci = {
        .step = state->calib_step,
        .event = in->calib_event,
        .rc_valid = rc_is_valid,
        .timeout = in->calib_timeout,
    };
    calib_outputs co = calib_step_next(&ci);
    state->calib_step = co.step;
    state->throttle_ramp = 0;

    if (co.exit != CALIB_EXIT_NONE) {
        *next_state = calib_exit_state(co.exit);
    }
    return clamp_esc(co.esc_us);
}

/* Resolve the ESC output and the next state, branching on calibration. In
 * ESC_CALIBRATION the throttle chain is bypassed: the ESC is driven by the
 * calibration sequence constants (still through the SI-3 hard clamp), and the
 * abort rules may override the next state. Otherwise the throttle chain runs. */
static uint32_t resolve_esc(const loop_inputs *in, const settings_params *params,
                            bool rc_is_valid, const sm_outputs *sm,
                            loop_state *state, sm_state *next_state)
{
    bool entering_calib = state->state != SM_STATE_ESC_CALIBRATION &&
                          sm->state == SM_STATE_ESC_CALIBRATION;
    if (entering_calib) {
        state->calib_step = CALIB_STEP_NEUTRAL;
    }
    if (sm->state == SM_STATE_ESC_CALIBRATION) {
        return run_calibration(in, rc_is_valid, state, next_state);
    }
    return throttle_chain_step(in->ch2.width_us, sm->throttle_target, params,
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
    state->state = next_state;

    loop_outputs out = {
        .esc_us = esc_us,
        .servo_us = servo_us,
        .telemetry = {
            .state = next_state,
            .rc_valid = rc_is_valid,
            .esc_us = esc_us,
            .servo_us = servo_us,
        },
    };
    return out;
}
