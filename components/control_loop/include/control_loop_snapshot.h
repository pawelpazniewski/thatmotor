#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings_validate.h"  /* settings_source */
#include "state_machine.h"      /* sm_state, sm_arm_reason */

/*
 * IDF-free definition of the web-panel telemetry snapshot, split out of
 * control_loop.h so pure consumers (e.g. telemetry_json) and host tests can
 * include the struct without pulling esp_err.h. control_loop.h re-includes this
 * header, so existing users are unaffected.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lossy telemetry snapshot for the web panel (R16/R12). The control loop writes
 * the newest snapshot each cycle into a single slot; readers (WS push) copy the
 * latest and tolerate skipped frames. Raw RC widths include CH4 (diagnostic,
 * outside RC_valid). Provenance flags mirror the load-time settings result.
 */
typedef struct {
    sm_state state;            /* current control state (R8) */
    sm_arm_reason arm_reason;  /* why arming is blocked, else READY (R7 gate) */
    bool rc_valid;             /* debounced RC validity */
    uint32_t ch1_us;           /* steering raw pulse width */
    uint32_t ch2_us;           /* throttle raw pulse width */
    uint32_t ch4_us;           /* diagnostic raw pulse width (R12) */
    uint32_t ch3_us;           /* diagnostic raw pulse width (future spot lock) */
    uint32_t ch1_period_us;    /* DIAG: measured CH1 frame period */
    uint32_t ch2_period_us;    /* DIAG: measured CH2 frame period */
    bool ch1_valid;            /* DIAG: CH1 passes channel_valid this frame */
    bool ch2_valid;            /* DIAG: CH2 passes channel_valid this frame */
    uint32_t servo_us;         /* commanded servo pulse (post-clamp) */
    uint32_t esc_us;           /* commanded ESC pulse (post-clamp) */
    int16_t servo_trim_us;     /* active signed servo neutral trim (live) */
    settings_source source;    /* R16: provenance */
    bool settings_valid;       /* R16 */
    bool calibrated;           /* R16: false -> UNCALIBRATED */
    bool defaults_used;        /* R16 */
    bool nvs_error;            /* R16 */
    /* GPS (diagnostic, OUTSIDE failsafe): copied from the GPS task's shared
     * state for the panel. Never feeds rc_valid/loop_step/sm_inputs. */
    bool gps_fix;              /* GPS has a usable fix */
    bool gps_fresh;            /* GPS freshness window still open (fresh != fix) */
    uint8_t gps_sats;          /* satellites used in the fix */
    int32_t gps_lat_e7;        /* latitude in degrees * 1e7 (negative for S) */
    int32_t gps_lon_e7;        /* longitude in degrees * 1e7 (negative for W) */
    uint16_t gps_speed_cms;    /* ground speed in cm/s */
    /* IMU / compass (BNO085, diagnostic, OUTSIDE failsafe): copied from the IMU
     * task's shared state for the panel. Never feeds rc_valid/loop_step/sm_inputs. */
    bool imu_ok;               /* fresh rotation-vector data is flowing */
    uint16_t imu_heading_deg10;/* yaw / heading in degrees * 10, [0, 3599] */
    uint8_t imu_calib;         /* SH-2 accuracy / calibration status, 0..3 */
    /* Spot-lock (CH3 GPS position hold) telemetry. State 0=off, 1=active,
     * 2=paused. err/bearing are meaningful while active/paused. */
    uint8_t spot_lock_state;        /* spot_lock_substate this cycle (0/1/2) */
    uint16_t spot_lock_err_m;       /* position error to target, metres */
    uint16_t spot_lock_bearing_deg10;/* bearing to target, degrees * 10 */
    /* Most recent CH3 entry attempt (diagnostic, OUTSIDE failsafe/control): a
     * successful attempt is already visible via spot_lock_state/the blackbox
     * session, this exists so a REJECTED entry is diagnosable too. seq bumps on
     * every CH3 rising edge (armed or not); the other fields snapshot the entry
     * gate at that same edge and hold until the next one. */
    uint32_t spot_lock_attempt_seq;        /* monotonic CH3 entry-attempt id */
    bool spot_lock_attempt_ok;             /* most recent attempt entered HOLD */
    bool spot_lock_attempt_armed;          /* control state was ARMED at it */
    bool spot_lock_attempt_sticks_neutral; /* both sticks were neutral at it */
    bool spot_lock_attempt_gps_fresh;      /* GPS freshness window open at it */
    bool spot_lock_attempt_gps_fix;        /* GPS had a usable fix at it */
    /* App-driven goto telemetry (R8). goto_state 0=off, 1=active, 2=paused, and
     * is non-zero ONLY while SRC_GOTO owns the target (a CH3 hold reads off).
     * err/bearing/arrived are meaningful while goto active/paused; the target is
     * the staged external point (RAM only). app_link_fresh mirrors the comms
     * watchdog. These field names are the stable telemetry contract for the iOS
     * app (parity with /api WS). */
    uint8_t goto_state;             /* goto sub-state this cycle (0/1/2) */
    int32_t goto_target_lat_e7;     /* staged goto target latitude (deg * 1e7) */
    int32_t goto_target_lon_e7;     /* staged goto target longitude (deg * 1e7) */
    uint16_t goto_err_m;            /* position error to goto target, metres */
    uint16_t goto_bearing_deg10;    /* bearing to goto target, degrees * 10 */
    bool goto_arrived;              /* err <= deadband while goto ACTIVE */
    bool app_link_fresh;            /* app link freshness (comms watchdog, R5) */
} control_loop_snapshot;

#ifdef __cplusplus
}
#endif
