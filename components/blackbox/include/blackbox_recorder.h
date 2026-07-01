#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Background recorder task for the spot-lock blackbox (Unit 3). Follows the
 * gps_reader pattern: a low-priority task (prio 2) started from app_main after
 * control_loop_init, entirely OUTSIDE the 50 Hz control loop and failsafe.
 *
 * Each tick (~2 Hz) it best-effort peeks control_loop_get_snapshot(), turns the
 * spot-lock substate transition into a session/sample decision (blackbox_sampler)
 * and writes headers/samples to flash (blackbox HAL). It is a pure OBSERVER: it
 * never feeds rc_valid/sm_inputs, never blocks the control loop, and a flash
 * write error is logged and swallowed (best-effort), never fatal.
 */

/**
 * Initialise the blackbox flash HAL and start the recorder task.
 *
 * @return ESP_OK on success; ESP_ERR_NOT_FOUND if the `spotlog` partition is
 *         missing; ESP_ERR_NO_MEM if the task could not be created.
 */
esp_err_t blackbox_recorder_start(void);

#ifdef __cplusplus
}
#endif
