#include "esc_calibration.h"

/* Constant ESC pulse widths emitted per calibration step (microseconds). The
 * WP880 learns its range from these; they bypass the throttle chain but pass
 * through the SI-3 hard clamp at the call site. */
#define CALIB_NEUTRAL_US 1500U
#define CALIB_FORWARD_US 2000U
#define CALIB_REVERSE_US 1000U

uint32_t calib_neutral_us(void)
{
    return CALIB_NEUTRAL_US;
}

uint32_t calib_us_for_step(calib_step step)
{
    switch (step) {
    case CALIB_STEP_FORWARD:
        return CALIB_FORWARD_US;
    case CALIB_STEP_REVERSE:
        return CALIB_REVERSE_US;
    case CALIB_STEP_NEUTRAL:
    case CALIB_STEP_DONE:
    default:
        return CALIB_NEUTRAL_US;
    }
}

/* The step that follows the operator's NEXT confirmation. REVERSE is the last
 * controllable step; advancing past it completes the sequence (DONE). */
static calib_step step_after(calib_step step)
{
    switch (step) {
    case CALIB_STEP_NEUTRAL:
        return CALIB_STEP_FORWARD;
    case CALIB_STEP_FORWARD:
        return CALIB_STEP_REVERSE;
    case CALIB_STEP_REVERSE:
    case CALIB_STEP_DONE:
    default:
        return CALIB_STEP_DONE;
    }
}

/* Abort to DISARMED, emitting neutral so the ESC is parked safely on exit. */
static calib_outputs exit_to_disarmed(void)
{
    calib_outputs out = {
        .step = CALIB_STEP_DONE,
        .esc_us = CALIB_NEUTRAL_US,
        .exit = CALIB_EXIT_TO_DISARMED,
    };
    return out;
}

/* Abort to FAILSAFE on RC loss; the failsafe path soft-stops the ESC. */
static calib_outputs exit_to_failsafe(void)
{
    calib_outputs out = {
        .step = CALIB_STEP_DONE,
        .esc_us = CALIB_NEUTRAL_US,
        .exit = CALIB_EXIT_TO_FAILSAFE,
    };
    return out;
}

/* Hold or advance the sequence, emitting the constant for the resulting step. */
static calib_outputs advance(const calib_inputs *in)
{
    calib_step next = in->event == CALIB_EVENT_NEXT ? step_after(in->step)
                                                    : in->step;
    if (next == CALIB_STEP_DONE) {
        return exit_to_disarmed();
    }
    calib_outputs out = {
        .step = next,
        .esc_us = calib_us_for_step(next),
        .exit = CALIB_EXIT_NONE,
    };
    return out;
}

calib_outputs calib_step_next(const calib_inputs *in)
{
    if (!in->rc_valid) {
        return exit_to_failsafe();
    }
    if (in->timeout || in->event == CALIB_EVENT_CANCEL) {
        return exit_to_disarmed();
    }
    return advance(in);
}
