#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esc_calibration.h"
#include "rc_sample.h"
#include "rc_validity.h"
#include "safety_clamp.h"
#include "settings_model.h"
#include "signal_chain.h"
#include "spot_lock.h"
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
    /* GPS + IMU: spot-lock control inputs ONLY (consumed by spot_lock_step in the
     * ARMED branch). They NEVER feed rc_valid / channel_valid / sm_inputs /
     * failsafe -- losing them pauses spot-lock, it does not trip failsafe. */
    bool gps_fresh;            /* GPS freshness predicate (R5) */
    bool gps_has_fix;          /* real fix quality (>0) (R3) */
    int32_t gps_lat_e7;        /* current latitude, degrees * 1e7 */
    int32_t gps_lon_e7;        /* current longitude, degrees * 1e7 */
    bool imu_ok;               /* heading data fresh (R5) */
    uint16_t imu_heading_deg10;/* current bow heading, degrees * 10, [0, 3599] */
    /* App-driven goto: source-of-target request + external target + link
     * freshness. Same contract as GPS/IMU: consumed by spot_lock_step ONLY in the
     * ARMED branch, NEVER fed to rc_valid / channel_valid / sm_inputs / failsafe.
     * A stale app link does NOT pause goto (R3/R4): goto is a latched intent, the
     * RC is the sole failsafe. comms_fresh is the retarget-in-flight / re-latch
     * gate, not a link failsafe. The target is HTTP-validated upstream; a fresh
     * link never carries a zeroed target. */
    bool goto_engage;          /* app goto latch (SRC_GOTO request, R3) */
    int32_t goto_lat_e7;       /* external goto target latitude, degrees * 1e7 */
    int32_t goto_lon_e7;       /* external goto target longitude, degrees * 1e7 */
    bool comms_fresh;          /* app link freshness: re-latch gate (R1), not a
                                * pause/failsafe input */
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
    spot_lock_state spot_lock; /* spot-lock sub-state + target snapshot */
    /* Most recent CH3 entry attempt, latched on each spot_lock_switch_edge_on
     * (armed or not) and held until the next one; mirrored into telemetry
     * unchanged every cycle in between. See loop_telemetry for field meaning. */
    uint32_t spot_lock_attempt_seq;
    bool spot_lock_attempt_ok;
    bool spot_lock_attempt_armed;
    bool spot_lock_attempt_sticks_neutral;
    bool spot_lock_attempt_gps_fresh;
    bool spot_lock_attempt_gps_fix;
} loop_state;

/** Telemetry snapshot produced each cycle (read-only view for the web panel). */
typedef struct {
    sm_state state;
    bool rc_valid;            /* debounced RC validity this cycle */
    uint32_t esc_us;          /* commanded ESC pulse width (post-clamp) */
    uint32_t servo_us;        /* commanded servo pulse width (post-clamp) */
    sm_arm_reason arm_reason; /* why arming is blocked this cycle (R7 gate) */
    uint8_t spot_lock_substate;      /* spot_lock_substate this cycle (0/1/2) */
    uint16_t spot_lock_err_m;        /* position error to target, metres */
    uint16_t spot_lock_bearing_deg10;/* bearing to target, degrees * 10 */
    /* Most recent CH3 entry attempt (diagnostic): seq bumps on every CH3 rising
     * edge (armed or not, R12-style -- outside rc_valid/failsafe); the other
     * fields snapshot the entry gate at that same edge and hold until the next
     * one. A successful attempt is already visible via spot_lock_substate/the
     * blackbox session; this exists so a REJECTED one is diagnosable too. */
    uint32_t spot_lock_attempt_seq;
    bool spot_lock_attempt_ok;
    bool spot_lock_attempt_armed;
    bool spot_lock_attempt_sticks_neutral;
    bool spot_lock_attempt_gps_fresh;
    bool spot_lock_attempt_gps_fix;
    /* App-driven goto telemetry (R8). Distinct from the shared spot_lock_* view:
     * these are non-zero ONLY while SRC_GOTO owns the target this cycle (a CH3
     * hold reads goto_substate = off), so the app sees goto activity specifically.
     * The field names are the stable telemetry contract for the iOS app. */
    uint8_t goto_substate;           /* goto sub-state this cycle (0/1/2) */
    uint16_t goto_err_m;             /* position error to goto target, metres */
    uint16_t goto_bearing_deg10;     /* bearing to goto target, degrees * 10 */
    bool goto_arrived;               /* err <= deadband while goto ACTIVE */
} loop_telemetry;

/** Actuator commands plus telemetry for one cycle. */
typedef struct {
    uint32_t esc_us;
    uint32_t servo_us;
    loop_telemetry telemetry;
    /* Signal to the orchestration layer that the goto engage latch must be
     * cleared this cycle: the throttle-hold manual-abort gesture (100%
     * deflection held ~3 s, R4 revision) or a physical CH3 preempt permanently
     * ends goto (no auto-resume; a fresh app goto command is required). A
     * link/GPS pause does NOT set this, so a transient link loss keeps the
     * latch and resumes. Computed only inside the ARMED branch. */
    bool goto_latch_clear;
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

/**
 * Whether the live servo neutral trim (step left/right, RAM-only) may be
 * applied this cycle: DISARMED (docked adjustment) or ARMED (on-water
 * correction while driving). NVS persistence is a separate, stricter gate
 * (loop_should_apply_pending / R17): a trim nudge taken here while ARMED still
 * only commits to flash once the unit returns to DISARMED, so the RT loop
 * never blocks on flash I/O while armed.
 *
 * @param state  Current control state.
 * @return true when state is DISARMED or ARMED.
 */
bool loop_trim_allowed(sm_state state);

#ifdef __cplusplus
}
#endif
