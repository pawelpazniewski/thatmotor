#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "nmea_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * GPS receiver (u-blox NEO-M9N over UART, NMEA). DIAGNOSTIC ONLY: the data is
 * surfaced in telemetry for the panel and is COMPLETELY OUTSIDE failsafe. It
 * never touches rc_valid, channel_valid, sm_inputs, or loop_step. Losing the
 * GPS has zero effect on arming/steering/failsafe. Mirrors the CH3 contract.
 *
 * The UART is read in a dedicated low-priority FreeRTOS task so it can never
 * block the 50 Hz control loop. The shared gps_state is updated under a short
 * mutex; gps_get_state copies it out non-blockingly for the telemetry snapshot.
 */

/**
 * Initialise UART1 (TX=GPIO17, RX=GPIO16, 38400 8N1) and start the reader task.
 * The GPS is optional: callers should log and continue on error rather than
 * abort the boot, since the GPS is outside failsafe.
 *
 * @return ESP_OK on success, otherwise the failing esp_err_t.
 */
esp_err_t gps_start(void);

/**
 * Copy the latest GPS state under a short mutex. Safe to call from the control
 * task; best-effort and non-blocking (skips the copy if the mutex is busy, in
 * which case @p out is left zeroed). Never feeds any control decision.
 *
 * @param out  Destination state (must be non-NULL).
 */
void gps_get_state(gps_state *out);

#ifdef __cplusplus
}
#endif
