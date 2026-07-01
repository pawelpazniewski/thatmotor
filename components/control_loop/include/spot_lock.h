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
 * a new sm_state). It snapshots "here and now" on CH3 rising edge and keeps the
 * kayak near that point by steering the bow at the target and adding forward
 * thrust proportional to distance. It NEVER drives reverse, NEVER holds a bow
 * heading, and unconditionally yields to RC failsafe (the integration layer
 * only invokes it while sm.state == ARMED).
 *
 * Output commands are NORMALIZED (the same convention the signal chain expects):
 *   - servo_cmd: signed, 0 = center, +/- full-scale = hard over.
 *   - throttle_cmd: forward only, 0 = neutral, up to the configured cap.
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
    bool comms_fresh;    /* app link freshness (R5): gates SRC_GOTO only */
} spot_lock_inputs;

/** Tunable regulator parameters (mapped from settings by the integration). */
typedef struct {
    uint16_t deadband_m;          /* position deadband radius, metres (R6) */
    uint16_t max_throttle_norm;   /* forward throttle cap, normalized (R7) */
    uint16_t throttle_gain_per_m; /* normalized throttle per metre of error */
    uint16_t servo_gain_per_deg;  /* normalized servo per degree of bearing err */
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
    int32_t throttle_cmd;        /* normalized forward [0, cap], 0 = neutral */
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
 *      link is fresh (R1: a fresh link tracks a newly commanded point); gated by
 *      comms_fresh in addition to GPS/IMU (R3/R5). During a link pause the core
 *      RETAINS the last good ref_* and never overwrites it from the input, so
 *      target retention across a link gap does not depend on the upstream latch.
 *   4. otherwise -> OFF.
 *
 * Within an engaged source:
 *   - PAUSED when !gps_fresh || !imu_ok || !gps_has_fix (SRC_HOLD/SRC_GOTO), or
 *     additionally !comms_fresh for SRC_GOTO only (link loss never pauses the
 *     RC-owned SRC_HOLD, R5); target retained, recovers to ACTIVE on return.
 *   - ACTIVE: within deadband -> neutral+center (R6); outside -> steer toward
 *     target and add forward thrust (capped, R7) only while the bearing error
 *     is within the +/-60 deg gate (R2). arrived = err_m <= deadband_m.
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
