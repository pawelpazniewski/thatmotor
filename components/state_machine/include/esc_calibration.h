#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ESC range calibration sequence (Unit 9), pure logic.
 *
 * A first-class service mode (state SM_STATE_ESC_CALIBRATION) that drives the
 * WP880 ESC through a fixed, operator-stepped range-learn sequence using
 * CONSTANT pulse widths emitted directly to the ESC pin:
 *
 *   NEUTRAL (1500 us) -> FORWARD (2000 us) -> REVERSE (1000 us) -> DONE.
 *
 * The sequence bypasses the throttle signal chain (no mapping / limit / ramp)
 * but every emitted value still passes through the SI-3 hard clamp at the call
 * site, so the service mode can never push the ESC outside the sanity window.
 *
 * Safety (R15 / SI-5):
 *  - The sequence NEVER starts automatically. Entry is gated by the state
 *    machine (DISARMED + RC valid + throttle neutral + explicit UI action +
 *    confirmed warning). This module only owns the stepping and abort rules.
 *  - Aborts: RC loss -> FAILSAFE; idle timeout -> DISARMED + neutral; operator
 *    cancel -> DISARMED + neutral.
 *
 * No I/O, no globals, no IDF dependencies: fully host-testable.
 */

/** Calibration sequence step (discriminated). */
typedef enum {
    CALIB_STEP_NEUTRAL = 0, /* emit 1500 us, wait for operator to advance */
    CALIB_STEP_FORWARD = 1, /* emit 2000 us */
    CALIB_STEP_REVERSE = 2, /* emit 1000 us */
    CALIB_STEP_DONE = 3,    /* sequence complete, leave to DISARMED + neutral */
} calib_step;

/** Operator event for one calibration cycle (discriminated). */
typedef enum {
    CALIB_EVENT_NONE = 0,   /* hold current step */
    CALIB_EVENT_NEXT = 1,   /* operator confirmed the current step, advance */
    CALIB_EVENT_CANCEL = 2, /* operator aborted the sequence */
} calib_event;

/** How (and whether) the sequence leaves the calibration state this cycle. */
typedef enum {
    CALIB_EXIT_NONE = 0,         /* stay in calibration */
    CALIB_EXIT_TO_DISARMED = 1,  /* done / cancel / timeout -> DISARMED+neutral */
    CALIB_EXIT_TO_FAILSAFE = 2,  /* RC loss -> FAILSAFE */
} calib_exit;

/** Inputs to one calibration step (pure). */
typedef struct {
    calib_step step;    /* current step (carry-over) */
    calib_event event;  /* operator event this cycle */
    bool rc_valid;      /* debounced RC validity this cycle */
    bool timeout;       /* idle timeout elapsed this cycle */
} calib_inputs;

/** Result of one calibration step (pure). */
typedef struct {
    calib_step step;  /* next step (valid when exit == CALIB_EXIT_NONE) */
    uint32_t esc_us;  /* ESC pulse width to emit this cycle (pre-clamp) */
    calib_exit exit;  /* whether/how to leave the calibration state */
} calib_outputs;

/** The ESC pulse width the sequence starts at (also the exit/idle neutral). */
uint32_t calib_neutral_us(void);

/**
 * Map a calibration step to its constant ESC pulse width in microseconds.
 *
 * NEUTRAL -> 1500, FORWARD -> 2000, REVERSE -> 1000, DONE -> 1500 (neutral).
 * The value is NOT clamped here; the caller routes it through clamp_pwm_us so
 * the SI-3 hard clamp remains the single, unconditional output boundary.
 *
 * @param step  Calibration step.
 * @return Constant pulse width in microseconds for that step.
 */
uint32_t calib_us_for_step(calib_step step);

/**
 * Advance the calibration sequence by one cycle (pure).
 *
 * Abort precedence: RC loss dominates (-> FAILSAFE), then idle timeout and
 * operator cancel (-> DISARMED + neutral), then NEXT advances the step. The
 * step after REVERSE is DONE, which also exits to DISARMED + neutral.
 *
 * @param in  Current step, operator event, RC validity and timeout flag.
 * @return Next step, the ESC pulse width to emit, and the exit decision.
 */
calib_outputs calib_step_next(const calib_inputs *in);

#ifdef __cplusplus
}
#endif
