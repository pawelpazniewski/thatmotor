#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * BNO085 compass (GY-BN008X board) over I2C, speaking SHTP/SH-2. DIAGNOSTIC
 * ONLY: heading + calibration status are surfaced in telemetry for the panel
 * and are COMPLETELY OUTSIDE failsafe. The IMU never touches rc_valid,
 * channel_valid, sm_inputs, or loop_step. Losing the compass has zero effect
 * on arming/steering/failsafe. Mirrors the GPS/CH3 contract.
 *
 * The sensor is serviced in a dedicated low-priority FreeRTOS task so it can
 * never block the 50 Hz control loop. The shared imu_state is updated under a
 * short mutex; imu_get_state copies it out non-blockingly for the snapshot.
 */

/**
 * Latest decoded IMU state. Heading is in degrees * 10, range [0, 3599].
 * calib is the SH-2 accuracy field (0..3; 3 = high). ok is true while fresh
 * Rotation Vector reports are arriving; on timeout/error it is false (a panel
 * flag only, NOT a failsafe input).
 */
typedef struct {
    bool ok;                /* true while fresh rotation-vector data is flowing */
    uint16_t heading_deg10; /* yaw / heading in degrees * 10, [0, 3599] */
    uint8_t calib;          /* SH-2 accuracy / calibration status, 0..3 */
} imu_state;

/**
 * Initialise I2C, reset the BNO085, enable the Rotation Vector report over
 * SHTP, and start the reader task. The IMU is optional: callers should log and
 * continue on error rather than abort the boot, since it is outside failsafe.
 *
 * @return ESP_OK on success, otherwise the failing esp_err_t.
 */
esp_err_t imu_start(void);

/**
 * Copy the latest IMU state under a short mutex. Safe to call from the control
 * task; best-effort and non-blocking (skips the copy if the mutex is busy, in
 * which case @p out is left zeroed). Never feeds any control decision.
 *
 * @param out  Destination state (must be non-NULL).
 */
void imu_get_state(imu_state *out);

#ifdef __cplusplus
}
#endif
