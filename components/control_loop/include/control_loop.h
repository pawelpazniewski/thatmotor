#pragma once

#include "esp_err.h"
#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Control cycle target rate. ~50 Hz: a 20 ms period matches the RC frame rate
 * and the LEDC update granularity. */
#define CONTROL_LOOP_PERIOD_MS 20U

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
esp_err_t control_loop_init(const settings_params *initial);

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
