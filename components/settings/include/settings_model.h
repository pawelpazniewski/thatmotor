#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump whenever the on-NVS layout of settings_params changes. A stored blob
 * with a different schema_version is rejected at load (-> defaults).
 * v2: split the symmetric max_throttle_pct into asymmetric forward/reverse
 * power limits (max_throttle_fwd_pct / max_throttle_rev_pct). */
#define SETTINGS_SCHEMA_VERSION 2U

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
} settings_params;

#ifdef __cplusplus
}
#endif
