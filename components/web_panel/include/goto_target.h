#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure, framework-agnostic validation of an external navigation target (goto).
 *
 * The app sends a target as degrees * 1e7 (int32, no floats on the wire, matching
 * gps_lat_e7). This module owns the range oracle: latitude within +/-90 deg,
 * longitude within +/-180 deg. It has NO IDF / no cJSON dependency so the target
 * contract is fully host-testable; the HTTP layer stays a thin cJSON + envelope
 * adapter that delegates the accept/reject decision here (fail-fast at the API
 * boundary: an out-of-range target never reaches the control loop).
 */

/* Inclusive valid range for a target latitude in degrees * 1e7 (+/-90 deg). */
#define GOTO_LAT_E7_MIN (-900000000)
#define GOTO_LAT_E7_MAX (900000000)
/* Inclusive valid range for a target longitude in degrees * 1e7 (+/-180 deg). */
#define GOTO_LON_E7_MIN (-1800000000)
#define GOTO_LON_E7_MAX (1800000000)

/**
 * Whether a navigation target is inside the valid geographic range.
 *
 * @param lat_e7  Target latitude in degrees * 1e7.
 * @param lon_e7  Target longitude in degrees * 1e7.
 * @return true when lat_e7 in [GOTO_LAT_E7_MIN, GOTO_LAT_E7_MAX] and lon_e7 in
 *         [GOTO_LON_E7_MIN, GOTO_LON_E7_MAX]; false otherwise.
 */
bool goto_target_valid(int32_t lat_e7, int32_t lon_e7);

#ifdef __cplusplus
}
#endif
