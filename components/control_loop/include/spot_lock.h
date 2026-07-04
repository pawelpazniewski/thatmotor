#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure, framework-agnostic spot-lock decision core (no IDF, host-testable).
 * Uses only geo_math + <math.h> internally; this header pulls in no esp_ or
 * driver headers (Pure _|_ HAL: grep-checkable).
 *
 * Spot-lock is a SUB-STATE that lives WITHIN the ARMED control state (it is not
 * a new sm_state). It keeps the kayak near a target point, with a drive law that
 * depends on the target source:
 *   - HOLD (CH3 "here and now" snapshot): OMNIDIRECTIONAL. Aims the bow-mounted
 *     motor at the target (or, when the target is more than +/-90 deg astern, at
 *     the reversed bearing) and applies thrust forward OR reverse, whichever
 *     swings the hull less -- fine for small holding corrections.
 *   - GOTO (app-driven navigate-to-point): FORWARD-ONLY, "turn the bow first,
 *     then go". Reverse is unsafe while travelling (you cannot see your track),
 *     so GOTO steers the bow the shortest way onto the target and only creeps
 *     forward until it is lined up (within a +/-cone), then drives to full cruise.
 * There is NO forward-thrust gate either way, so the hull can never stall unable
 * to rotate. It does NOT hold a bow heading for its own sake, and unconditionally
 * yields to RC failsafe (the integration layer only invokes it while ARMED).
 *
 * Output commands are NORMALIZED (the same convention the signal chain expects):
 *   - servo_cmd: signed, 0 = center, +/- full-scale = hard over.
 *   - throttle_cmd: SIGNED, 0 = neutral, positive = forward and negative =
 *     reverse, magnitude up to the configured cap.
 * The integration layer maps these through ramp/slew -> map_normalized_to_us ->
 * the SI-3 hard clamp; spot-lock itself touches no actuator.
 */

/** Normalized command full-scale magnitude (matches SIGNAL_NORMALIZED_FULL_SCALE
 * in signal_chain.h; kept local so this header stays decoupled from settings). */
#define SPOT_LOCK_CMD_FULL_SCALE 1000

/** Internal sub-state, distinct from sm_state. */
typedef enum {
    SPOT_LOCK_OFF = 0,    /* manual: spot-lock not driving the actuators */
    SPOT_LOCK_ACTIVE = 1, /* holding position: computing servo/throttle */
    SPOT_LOCK_PAUSED = 2, /* sensor data lost: neutral+center, target retained */
} spot_lock_substate;

/** Source of the active target (arbitration outcome for this cycle). CH3 hold
 * physically preempts an app-driven goto (SRC_HOLD wins over SRC_GOTO). */
typedef enum {
    SPOT_LOCK_SRC_NONE = 0, /* no source engaged: OFF */
    SPOT_LOCK_SRC_HOLD = 1, /* CH3 snapshot of "here and now" (RC-owned) */
    SPOT_LOCK_SRC_GOTO = 2, /* external app target (link-gated) */
} spot_lock_target_source;

/** Per-cycle inputs. All flags/coords come from the loop's read_inputs. */
typedef struct {
    bool armed;          /* sm.state == ARMED (override allowed only here) */
    bool ch3_on;         /* CH3 debounced level: spot-lock requested */
    bool ch3_edge_on;    /* CH3 rising edge this cycle (entry intent) */
    bool sticks_neutral; /* both CH1/CH2 within their neutral deadband (R3/R4) */
    bool gps_fresh;      /* GPS freshness predicate (R5) */
    bool gps_has_fix;    /* real fix quality (>0): guards the seed-fresh window
                          * so entry cannot occur on freshness alone (R3) */
    bool imu_ok;         /* heading data fresh (R5) */
    int32_t lat_e7;      /* current latitude, degrees * 1e7 */
    int32_t lon_e7;      /* current longitude, degrees * 1e7 */
    uint16_t heading_deg10; /* current bow heading, degrees * 10, [0, 3599] */
    bool goto_engage;    /* app goto latch (source-of-target request, R3) */
    int32_t goto_lat_e7; /* external goto target latitude, degrees * 1e7 */
    int32_t goto_lon_e7; /* external goto target longitude, degrees * 1e7 */
    bool comms_fresh;    /* app link freshness: NOT a failsafe/pause input. It is
                          * the retarget-in-flight gate - a fresh link re-latches
                          * ref_* to a newly commanded goto point; a stale link
                          * retains the last good target (null-island guard, R1). */
} spot_lock_inputs;

/** Tunable regulator parameters (mapped from settings by the integration). */
typedef struct {
    uint16_t deadband_m;          /* position deadband radius, metres (R6) */
    uint16_t max_throttle_norm;   /* forward throttle cap, normalized (R7) */
    uint16_t throttle_gain_per_m; /* normalized throttle per metre of error */
    uint16_t servo_gain_per_deg;  /* normalized servo per degree of bearing err */
    uint16_t goto_slowdown_distance_m; /* goto cruise-decel slowdown distance (m) */
    uint16_t goto_cruise_norm;    /* goto cruise ceiling, normalized
                                   * (= max_throttle_fwd_pct % of full scale) */
} spot_lock_params;

/** Carry-over state owned by the loop, updated in place each cycle. */
typedef struct {
    spot_lock_substate substate;          /* current sub-state */
    spot_lock_target_source target_source; /* which source owns the target */
    int32_t ref_lat_e7;                   /* target latitude (snapshot or goto) */
    int32_t ref_lon_e7;                   /* target longitude (snapshot or goto) */
} spot_lock_state;

/** Decision outputs for one cycle. */
typedef struct {
    spot_lock_substate substate; /* resulting sub-state (mirrors state) */
    int32_t throttle_cmd;        /* normalized signed, 0 = neutral, +fwd / -rev */
    int32_t servo_cmd;           /* normalized signed, 0 = center */
    uint16_t err_m;              /* position error in metres (telemetry) */
    uint16_t bearing_deg10;      /* bearing to target, deg*10 (telemetry) */
    bool arrived;                /* err_m <= deadband_m while ACTIVE (telemetry) */
} spot_lock_outputs;

/**
 * Run one spot-lock decision cycle (pure, deterministic).
 *
 * Source arbitration in ARMED (physical CH3 preempts app-driven goto):
 *   1. ANY -> OFF when !armed || !sticks_neutral (manual override, R4/R6).
 *   2. ch3_on -> SRC_HOLD: on entry (edge + fresh real fix) snapshot the
 *      current position as the target; a running goto is preempted here (R4).
 *   3. goto_engage && !ch3_on -> SRC_GOTO: target is the external goto point.
 *      ref_* is (re)latched from goto_* only on entry into SRC_GOTO or while the
 *      link is fresh (R1: a fresh link tracks a newly commanded point). A stale
 *      app link does NOT pause goto (R3/R4): it is a latched intent, only the
 *      RC failsafe ends it. While the link is stale the core RETAINS the last
 *      good ref_* and never overwrites it from the input, so target retention
 *      across a link gap does not depend on the upstream latch.
 *   4. otherwise -> OFF.
 *
 * Within an engaged source:
 *   - PAUSED when !gps_fresh || !imu_ok || !gps_has_fix (SRC_HOLD/SRC_GOTO): the
 *     SENSOR degradation domain. The app link is NOT a pause input for either
 *     source (R3/R4); target retained, recovers to ACTIVE on sensor return.
 *   - ACTIVE: within deadband -> neutral+center (R6); outside -> drive toward the
 *     target, by a law that depends on the source (arrived = err_m <= deadband_m):
 *       * SRC_HOLD: OMNIDIRECTIONAL. The bearing error is reduced to a +/-90 deg
 *         steering error by choosing forward OR reverse (whichever swings the hull
 *         less); the servo steers by that reduced error and thrust is applied in
 *         the chosen direction, tapered by cos(steering error) with a floor.
 *         Magnitude = P-throttle (gain x distance, capped to max_throttle_norm, R7).
 *       * SRC_GOTO: FORWARD-ONLY, turn-then-go. The servo steers by the full
 *         bearing error (shortest way to face the target); thrust is forward only,
 *         at a small creep while the bow is outside the +/-cone (so the boat pivots
 *         into line without reversing) and ramps to full as it lines up. Magnitude
 *         = cruise-decel profile (full goto_cruise_norm beyond goto_slowdown_distance_m,
 *         then linear down to the deadband edge). GOTO never reverses.
 *
 * @param in  Per-cycle inputs (must be non-NULL).
 * @param p   Regulator parameters (must be non-NULL).
 * @param st  Carry-over state, updated in place (must be non-NULL).
 * @return Normalized commands + telemetry for this cycle.
 */
spot_lock_outputs spot_lock_step(const spot_lock_inputs *in,
                                 const spot_lock_params *p,
                                 spot_lock_state *st);

#ifdef __cplusplus
}
#endif
