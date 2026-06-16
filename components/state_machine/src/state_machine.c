#include "state_machine.h"

/* Arming guard (R7): every condition must hold to leave DISARMED for ARMED. */
static bool can_arm(const sm_inputs *inputs)
{
    return inputs->rc_valid && inputs->throttle_neutral &&
           inputs->ui_arm_request && !inputs->calib_in_progress &&
           !inputs->settings_apply_in_progress;
}

/* Next state from DISARMED. RC loss dominates; otherwise the arm guard. */
static sm_state next_from_disarmed(const sm_inputs *inputs)
{
    if (!inputs->rc_valid) {
        return SM_STATE_FAILSAFE;
    }
    if (can_arm(inputs)) {
        return SM_STATE_ARMED;
    }
    return SM_STATE_DISARMED;
}

/* Next state from ARMED. RC loss -> FAILSAFE; explicit disarm -> DISARMED. */
static sm_state next_from_armed(const sm_inputs *inputs)
{
    if (!inputs->rc_valid) {
        return SM_STATE_FAILSAFE;
    }
    if (inputs->ui_disarm_request) {
        return SM_STATE_DISARMED;
    }
    return SM_STATE_ARMED;
}

/* Latched failsafe: persists while RC is invalid; the ONLY exit is RC recovery
 * to DISARMED (never straight to ARMED). */
static sm_state next_from_failsafe(const sm_inputs *inputs)
{
    if (inputs->rc_valid) {
        return SM_STATE_DISARMED;
    }
    return SM_STATE_FAILSAFE;
}

/* ESC calibration (sequence in Unit 9). RC loss still forces FAILSAFE. */
static sm_state next_from_calibration(const sm_inputs *inputs)
{
    if (!inputs->rc_valid) {
        return SM_STATE_FAILSAFE;
    }
    return SM_STATE_ESC_CALIBRATION;
}

static sm_state next_state(sm_state current, const sm_inputs *inputs)
{
    switch (current) {
    case SM_STATE_DISARMED:
        return next_from_disarmed(inputs);
    case SM_STATE_ARMED:
        return next_from_armed(inputs);
    case SM_STATE_FAILSAFE:
        return next_from_failsafe(inputs);
    case SM_STATE_ESC_CALIBRATION:
        return next_from_calibration(inputs);
    default:
        return SM_STATE_FAILSAFE; /* unknown state -> safe latch */
    }
}

/* Throttle tracks the stick ONLY in ARMED; every other state forces neutral so
 * the throttle ramp soft-stops the ESC (R6/R7). */
static throttle_target_mode throttle_target_for(sm_state state)
{
    if (state == SM_STATE_ARMED) {
        return THROTTLE_TARGET_TRACK;
    }
    return THROTTLE_TARGET_NEUTRAL;
}

/* Servo rule is independent of arming: RC valid -> track CH1, else center. */
static servo_target_mode servo_target_for(const sm_inputs *inputs)
{
    if (inputs->rc_valid) {
        return SERVO_TARGET_TRACK;
    }
    return SERVO_TARGET_CENTER;
}

sm_outputs sm_step(sm_state current, const sm_inputs *inputs)
{
    sm_state state = next_state(current, inputs);
    sm_outputs out = {
        .state = state,
        .throttle_target = throttle_target_for(state),
        .servo_target = servo_target_for(inputs),
    };
    return out;
}
