#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esc_calibration.h"
#include "esp_err.h"
#include "loop_step.h"
#include "settings_model.h"
#include "settings_validate.h"
#include "state_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CONTROL_LOOP_PERIOD_MS is defined in loop_step.h (the IDF-free header) and
 * re-exported here via that include so existing users keep working. */

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
    uint8_t gps_sats;          /* satellites used in the fix */
    int32_t gps_lat_e7;        /* latitude in degrees * 1e7 (negative for S) */
    int32_t gps_lon_e7;        /* longitude in degrees * 1e7 (negative for W) */
    uint16_t gps_speed_cms;    /* ground speed in cm/s */
    /* IMU / compass (BNO085, diagnostic, OUTSIDE failsafe): copied from the IMU
     * task's shared state for the panel. Never feeds rc_valid/loop_step/sm_inputs. */
    bool imu_ok;               /* fresh rotation-vector data is flowing */
    uint16_t imu_heading_deg10;/* yaw / heading in degrees * 10, [0, 3599] */
    uint8_t imu_calib;         /* SH-2 accuracy / calibration status, 0..3 */
} control_loop_snapshot;

/**
 * UI events posted by the web panel for the next control cycle. The loop is the
 * single consumer; events are consumed once (edge semantics). Calibration step
 * events use the discriminated calib_event enum from esc_calibration.
 */
typedef struct {
    bool arm_request;
    bool disarm_request;
    bool calib_request;
    bool calib_confirm;
    bool deploy_request;       /* panel "Deploy": enter DEPLOY from DISARMED */
    bool stow_request;         /* panel "Stow": leave DEPLOY -> DISARMED */
    bool trim_left;            /* servo neutral trim: step one click left */
    bool trim_right;           /* servo neutral trim: step one click right */
    bool trim_save;            /* persist the current servo trim to NVS */
    calib_event calib_event;   /* discriminated calibration operator event */
} control_loop_ui_events;

/**
 * Orchestration layer for the control loop (Unit 7). Thin HAL/timing wrapper
 * around the pure loop_step core: it owns the single mutable copy of the active
 * params (SI-6 single-writer), reads rc_capture samples, drives pwm_out, and
 * feeds the task watchdog only at the end of a completed iteration.
 *
 * Pending params from the web panel arrive through a length-1 mailbox
 * (xQueueOverwrite) and are applied to the active params ONLY while DISARMED,
 * with the DISARMED check re-evaluated at apply time (TOCTOU).
 */

/**
 * Initialise the control loop: seed active params, create the length-1 pending
 * mailbox, and prepare the carry-over state (DISARMED, safe seeds). Must be
 * called once, after pwm_out and rc_capture are initialised, before the loop
 * task starts.
 *
 * @param initial  Active params to start from (e.g. loaded NVS or defaults).
 * @return ESP_OK on success, otherwise the failing esp_err_t.
 */
esp_err_t control_loop_init(const settings_params *initial,
                            const settings_validation_result *load_result);

/**
 * Copy the latest telemetry snapshot for the web panel (lossy single slot).
 * Safe to call from another task; the read is a best-effort struct copy.
 *
 * @param out  Destination snapshot (must be non-NULL).
 */
void control_loop_get_snapshot(control_loop_snapshot *out);

/**
 * Copy the current active params for the panel's GET (read-only view).
 *
 * @param out  Destination params (must be non-NULL).
 */
void control_loop_get_active_params(settings_params *out);

/**
 * Post UI events (arm/disarm/calibration) for the loop to consume next cycle.
 * Length-1 mailbox semantics: only the latest event set survives. Non-blocking.
 *
 * @param events  UI events to stage (must be non-NULL).
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialised.
 */
esp_err_t control_loop_post_ui_events(const control_loop_ui_events *events);

/**
 * Post a validated pending params set to the loop's length-1 mailbox. The loop
 * applies it on a later cycle, but only while DISARMED. Overwrites any prior
 * pending set that has not yet been applied. Non-blocking, ISR-unsafe.
 *
 * @param pending  Validated params to stage (must be non-NULL).
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialised.
 */
esp_err_t control_loop_post_pending(const settings_params *pending);

/**
 * Run the control loop forever at ~50 Hz. Registers the calling task with the
 * task watchdog, then each cycle: peek+conditionally-apply pending, read RC,
 * run loop_step, write the actuators, and reset the watchdog ONLY after a
 * fully completed iteration. Never returns.
 */
void control_loop_run(void);

#ifdef __cplusplus
}
#endif
