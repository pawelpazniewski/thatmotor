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

/**
 * Validate a target given as raw doubles (as decoded from JSON) and, only when
 * valid, narrow it to int32 degrees * 1e7.
 *
 * JSON has no int32 type, so the wire value must be range-checked in the double
 * domain BEFORE the cast: a non-finite value (INF/NaN) casts to int as undefined
 * behavior, and an out-of-int32 magnitude (e.g. 4.39e9) would wrap modulo 2^32
 * back into the valid range and bypass the range oracle. The whole geographic
 * range (+/-1.8e9) is exactly representable in double, so the comparison against
 * GOTO_*_E7_MIN/MAX is exact.
 *
 * @param lat_d   Target latitude in degrees * 1e7, as a double.
 * @param lon_d   Target longitude in degrees * 1e7, as a double.
 * @param lat_e7  Out: narrowed latitude, written only on success.
 * @param lon_e7  Out: narrowed longitude, written only on success.
 * @return true when both values are finite and in range (outputs written);
 *         false otherwise (outputs untouched).
 */
bool goto_target_from_double(double lat_d, double lon_d, int32_t *lat_e7,
                             int32_t *lon_e7);

#ifdef __cplusplus
}
#endif
