#include "loop_step.h"

#include "signal_chain.h"

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
                                 const settings_params *params, bool rc_is_valid)
{
    sm_inputs si = {
        .rc_valid = rc_is_valid,
        .throttle_neutral = throttle_is_neutral(in->ch2.width_us, params),
        .calib_in_progress = false,
        .settings_apply_in_progress = false,
        .ui_arm_request = in->ui_arm_request,
        .ui_disarm_request = in->ui_disarm_request,
    };
    return si;
}

loop_outputs loop_step(const loop_inputs *in, const loop_validity_cfg *cfg,
                       const settings_params *params, loop_state *state)
{
    bool rc_is_valid = resolve_rc_valid(in, cfg, state);

    sm_inputs si = build_sm_inputs(in, params, rc_is_valid);
    sm_outputs sm = sm_step(state->state, &si);
    state->state = sm.state;

    uint32_t esc_us = throttle_chain_step(in->ch2.width_us, sm.throttle_target,
                                          params, &state->throttle_ramp);
    uint32_t servo_us = servo_chain_step(in->ch1.width_us, sm.servo_target,
                                         params, &state->servo_slew);

    loop_outputs out = {
        .esc_us = esc_us,
        .servo_us = servo_us,
        .telemetry = {
            .state = sm.state,
            .rc_valid = rc_is_valid,
            .esc_us = esc_us,
            .servo_us = servo_us,
        },
    };
    return out;
}
