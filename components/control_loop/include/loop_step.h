#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esc_calibration.h"
#include "rc_sample.h"
#include "rc_validity.h"
#include "safety_clamp.h"
#include "settings_model.h"
#include "signal_chain.h"
#include "state_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Control cycle target rate. ~50 Hz: a 20 ms period matches the RC frame rate
 * and the LEDC update granularity. Defined here (the IDF-free pure-logic header)
 * so host-testable code can derive cycle counts from it without pulling in the
 * IDF-dependent control_loop.h; control_loop.h re-exports it via this header. */
#define CONTROL_LOOP_PERIOD_MS 20U

/**
 * Pure logical core of the control loop (Unit 7).
 *
 * One call performs one ~50 Hz cycle end to end with NO I/O and NO IDF
 * dependencies, so the whole cross-layer integration is host-testable:
 *
 *   channel_valid + debounce -> rc_valid -> sm_step
 *     -> throttle_chain_step (CH2 -> ESC) + servo_chain_step (CH1 -> servo).
 *
 * All mutable carry-over (debounce counters, ramp/slew accumulators, the state)
 * lives in loop_state and is updated in place. The active params are read-only
 * here; the single writer of active params is the orchestration layer
 * (control_loop.c), not this function (SI-6).
 */

/** Inputs that change every cycle. Raw samples come straight from rc_capture. */
typedef struct {
    rc_channel_sample ch1; /* steering (CH1) raw capture sample */
    rc_channel_sample ch2; /* throttle (CH2) raw capture sample */
    uint32_t now_ticks;    /* current tick in the rc_capture domain (recency) */
    bool ui_arm_request;   /* explicit arm action from the panel this cycle */
    bool ui_disarm_request;/* explicit disarm action from the panel this cycle */
    bool ui_calib_request; /* explicit "start ESC calibration" action */
    bool ui_calib_confirm; /* operator confirmed the removal warning */
    bool deploy_request;   /* explicit "enter DEPLOY" (from DISARMED) this cycle */
    bool stow_request;     /* explicit "leave DEPLOY" -> DISARMED this cycle */
    calib_event calib_event;/* calibration step event (next/cancel) this cycle */
    bool calib_timeout;    /* calibration idle timeout elapsed this cycle */
    bool spot_lock_switch_on;      /* CH3 debounced level: spot-lock requested */
    bool spot_lock_switch_edge_on; /* CH3 rising edge this cycle (enter intent) */
} loop_inputs;

/** Per-channel validity thresholds (constant across cycles). */
typedef struct {
    rc_channel_cfg ch1;
    rc_channel_cfg ch2;
} loop_validity_cfg;

/** Mutable carry-over state owned by the loop, updated in place each cycle. */
typedef struct {
    sm_state state;
    rc_debounce_state rc_debounce;
    throttle_ramp_state throttle_ramp; /* ramped throttle command + dwell */
    int32_t servo_slew;    /* slewed servo pulse width (us) */
    calib_step calib_step; /* current ESC calibration step (when in calib) */
} loop_state;

/** Telemetry snapshot produced each cycle (read-only view for the web panel). */
typedef struct {
    sm_state state;
    bool rc_valid;            /* debounced RC validity this cycle */
    uint32_t esc_us;          /* commanded ESC pulse width (post-clamp) */
    uint32_t servo_us;        /* commanded servo pulse width (post-clamp) */
    sm_arm_reason arm_reason; /* why arming is blocked this cycle (R7 gate) */
} loop_telemetry;

/** Actuator commands plus telemetry for one cycle. */
typedef struct {
    uint32_t esc_us;
    uint32_t servo_us;
    loop_telemetry telemetry;
} loop_outputs;

/**
 * Initialise loop carry-over state: DISARMED, debounce latched valid, ramp at
 * neutral (0), slew seeded to the servo center so there is no startup transient.
 *
 * @param state             State to initialise (must be non-NULL).
 * @param params            Active params used to derive the servo center seed.
 * @param debounce_threshold Consecutive bad frames required to latch invalid.
 */
void loop_state_init(loop_state *state, const settings_params *params,
                     uint16_t debounce_threshold);

/**
 * Run one control cycle (pure).
 *
 * @param in      Per-cycle inputs (must be non-NULL).
 * @param cfg     Validity thresholds (must be non-NULL).
 * @param params  Active control params, read-only (must be non-NULL).
 * @param state   Carry-over state, updated in place (must be non-NULL).
 * @return Actuator commands (already past the SI-3 hard clamp) and telemetry.
 */
loop_outputs loop_step(const loop_inputs *in, const loop_validity_cfg *cfg,
                       const settings_params *params, loop_state *state);

/**
 * Route a calibration ESC value through the SI-3 hard clamp (SI-3 boundary on
 * the service-mode path). This is the SINGLE, unconditional clamp every
 * calibration constant passes through before reaching the ESC; the calibration
 * sequence has no branch that bypasses it. Exposed so the SI-3 invariant can be
 * proven behaviourally with an out-of-window value (a calibration constant that
 * exceeds a narrowed window MUST be snapped to the boundary).
 *
 * @param esc_us  Pre-clamp ESC pulse width (a calibration step constant).
 * @param window  Inclusive sanity window; bounds normalised if inverted.
 * @return Clamped pulse width within the inclusive window.
 */
uint32_t calib_clamp_esc(uint32_t esc_us, PwmWindow window);

/**
 * Whether a staged pending params set may be applied to the active params this
 * cycle (R17/SI-6): apply ONLY while DISARMED. The orchestration layer
 * re-evaluates this at apply time (TOCTOU) so a state change between peek and
 * apply cannot let a write through in ARMED/FAILSAFE.
 *
 * @param state  Current control state.
 * @return true only when state is DISARMED.
 */
bool loop_should_apply_pending(sm_state state);

#ifdef __cplusplus
}
#endif
