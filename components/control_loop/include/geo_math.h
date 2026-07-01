#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure, framework-agnostic local geodesy for spot-lock (no IDF, host-testable).
 * Only <math.h> is used internally; this header pulls in no esp_ or driver deps.
 *
 * Model: EQUIRECTANGULAR (flat-earth) approximation. Over the few-metre to
 * few-hundred-metre distances spot-lock cares about, the error of treating a
 * small lat/lon patch as a plane is negligible, and it avoids the cost/edge
 * cases of the full haversine. Coordinates are degrees * 1e7 (the project's GPS
 * convention, signed: S/W negative).
 *
 * Offsets are expressed in a local ENU-style frame centred on the SECOND
 * (reference) coordinate:
 *   north_m  = (lat - ref_lat) * metres-per-degree-latitude
 *   east_m   = (lon - ref_lon) * metres-per-degree-latitude * cos(ref_lat)
 * The cos(ref_lat) term shrinks east distances away from the equator (meridians
 * converge); omitting it overstates east error at high latitudes.
 */

/** Local planar offset of a point relative to a reference, in metres. */
typedef struct {
    float north_m; /* +north / -south displacement from the reference */
    float east_m;  /* +east / -west displacement from the reference */
} geo_offset;

/**
 * Planar offset of (@p lat_e7, @p lon_e7) relative to (@p ref_lat_e7,
 * @p ref_lon_e7). The cos() correction uses the reference latitude.
 *
 * @param lat_e7      Point latitude, degrees * 1e7.
 * @param lon_e7      Point longitude, degrees * 1e7.
 * @param ref_lat_e7  Reference latitude, degrees * 1e7 (frame origin).
 * @param ref_lon_e7  Reference longitude, degrees * 1e7 (frame origin).
 * @return North/East offset in metres (point minus reference).
 */
geo_offset geo_offset_m(int32_t lat_e7, int32_t lon_e7, int32_t ref_lat_e7,
                        int32_t ref_lon_e7);

/**
 * Straight-line distance of an offset from its reference.
 *
 * @param off  Planar offset (e.g. from geo_offset_m).
 * @return Distance in metres (>= 0); zero offset -> 0.
 */
float geo_distance_m(geo_offset off);

/**
 * Compass bearing of an offset's direction, degrees * 10 in [0, 3599].
 *
 * 0 = due north, 900 = east, 1800 = south, 2700 = west. A zero offset has no
 * defined direction; it returns 0 (a well-defined, tested value).
 *
 * @param off  Planar offset (e.g. from geo_offset_m).
 * @return Bearing in degrees * 10, range [0, 3599].
 */
uint16_t geo_bearing_deg10(geo_offset off);

#ifdef __cplusplus
}
#endif
