#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump whenever the on-NVS layout of settings_params changes. A stored blob
 * with a different schema_version is rejected at load (-> defaults).
 * v2: split the symmetric max_throttle_pct into asymmetric forward/reverse
 * power limits (max_throttle_fwd_pct / max_throttle_rev_pct).
 * v3: add the CH4 mode-switch button (ch4_mode_switch_enabled +
 * ch4_switch_threshold_us) so CH4 can toggle ARMED/DISARMED.
 * v4: add the manual DEPLOY mode (deploy_servo_us held while the motor is off)
 * and the CH4 click-gesture window (click_window_ms) that distinguishes a single
 * click (arm/disarm) from a triple click (deploy/stow).
 * v5: add the signed servo neutral trim (servo_trim_us): a mechanical-zero
 * correction added to the servo output before the hard clamp, so neutral,
 * endpoints and deploy all shift uniformly.
 * v6: add the spot-lock (CH3 GPS position hold) regulator parameters
 * (spot_lock_deadband_m, spot_lock_max_throttle_pct, spot_lock_throttle_gain,
 * spot_lock_servo_gain). */
#define SETTINGS_SCHEMA_VERSION 6U

/* Signed servo neutral trim bounds/step (public: the control loop drives the
 * panel's live Step Left/Right with these; the validator/defaults reuse them).
 * Range +/-300 us (~+/-40 deg on a 270 deg / 2000 us servo) is a generous
 * mechanical-misalignment budget; one panel step is ~7 us (~1 deg). */
#define SERVO_TRIM_MAX_US 300
#define SERVO_TRIM_STEP_US 7
#define SERVO_TRIM_DEFAULT 0

/**
 * Persisted control parameters.
 *
 * Framework-agnostic plain-old-data: no pointers, host-testable, serialisable
 * to a versioned NVS blob in a later unit. All pulse-width fields are in
 * microseconds, all rate fields in microseconds-per-control-cycle, times in
 * milliseconds.
 */
typedef struct {
    uint16_t schema_version;

    /* RC input calibration (per the steering/throttle sticks). */
    uint16_t rc_min_us;  /* stick full one way */
    uint16_t rc_mid_us;  /* stick centre */
    uint16_t rc_max_us;  /* stick full other way */

    /* Servo (steering) shaping. */
    uint16_t servo_slew_us_per_cycle; /* max servo change per control cycle */
    uint16_t servo_min_us;            /* steering endpoint minimum */
    uint16_t servo_max_us;            /* steering endpoint maximum */
    uint16_t steer_deadband_us;       /* deadband around centre (default 0) */
    bool servo_reverse;
    int16_t servo_trim_us;            /* SIGNED neutral trim added to servo out */

    /* Throttle (ESC) shaping. */
    uint16_t esc_ramp_up_us_per_cycle;   /* accel rate */
    uint16_t esc_ramp_down_us_per_cycle; /* decel rate */
    uint16_t throttle_deadband_us;       /* deadband around centre */
    uint16_t max_throttle_fwd_pct;       /* forward power limit, percent */
    uint16_t max_throttle_rev_pct;       /* reverse power limit, percent */
    bool throttle_reverse;

    /* ESC output calibration (maps logical command to WP880 pulse widths). */
    uint16_t esc_neutral_us;
    uint16_t esc_neutral_band_us;
    uint16_t esc_forward_min_us;
    uint16_t esc_forward_max_us;
    uint16_t esc_reverse_min_us;
    uint16_t esc_reverse_max_us;

    /* Safety / failsafe. */
    uint16_t failsafe_timeout_ms;
    uint16_t reverse_neutral_dwell_ms; /* dwell at neutral on fwd<->rev flip */

    /* CH4 position switch (GPIO32): high=arm, low=disarm, one flick=one change. */
    bool ch4_mode_switch_enabled;      /* CH4 drives arm/disarm when true */
    uint16_t ch4_switch_threshold_us;  /* width >= this (in RC band) = high */

    /* Manual DEPLOY mode (raise the motor). */
    uint16_t deploy_servo_us;          /* servo pulse held in DEPLOY (motor off) */
    uint16_t click_window_ms;          /* CH4 click-gesture window (1 vs 3 clicks) */

    /* Spot-lock (CH3 GPS position hold) regulator. All tunable in the panel;
     * gentle defaults (de-risk: stable hold on mild gains, tuned in the field).
     * The integration maps these onto the pure spot_lock_params each cycle. */
    uint16_t spot_lock_deadband_m;       /* hold radius, metres (R6) */
    uint16_t spot_lock_max_throttle_pct; /* forward thrust cap, percent (R7) */
    uint16_t spot_lock_throttle_gain;    /* normalized throttle per metre error */
    uint16_t spot_lock_servo_gain;       /* normalized servo per degree of bearing */
} settings_params;

#ifdef __cplusplus
}
#endif
