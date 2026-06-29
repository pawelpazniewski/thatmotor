#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decoded GPS fix state. Coordinates are in degrees scaled by 1e7 (signed: S/W
 * negative), speed in cm/s, course in whole degrees. Pure data, no float in the
 * stored result. Mirrors the small-integer telemetry style of the rest of the
 * project so the panel can render it directly.
 */
typedef struct {
    bool fix;             /* true when GGA reports a usable fix quality (>0) */
    bool fresh;           /* true while a fresh fix arrived within the staleness
                           * window; set by the reader task (NOT by the parser),
                           * mirrors imu_state.ok. Panel/spot-lock input only,
                           * never a failsafe input. */
    uint8_t sats;         /* satellites used in the fix */
    int32_t lat_e7;       /* latitude in degrees * 1e7 (negative for S) */
    int32_t lon_e7;       /* longitude in degrees * 1e7 (negative for W) */
    uint16_t speed_cms;   /* ground speed in cm/s */
    uint16_t course_deg;  /* course over ground in whole degrees */
} gps_state;

/**
 * Parse one NMEA sentence (a single line, '\0'-terminated, without trailing
 * CR/LF required) into @p st. PURE: no IDF, no I/O, host-testable.
 *
 * Supported sentences (any talker id, e.g. GP/GN/GL):
 *   - ??GGA -> fix quality, satellite count, latitude, longitude.
 *   - ??RMC -> ground speed (knots) and course; fix validity (A/V).
 *   - ??VTG -> ground speed (km/h) and course.
 *
 * The NMEA checksum (`*HH` after the `$...` body) is validated first. A bad
 * checksum, an unsupported sentence, or a malformed/incomplete field leaves
 * @p st untouched and returns false. On a successful parse the relevant fields
 * of @p st are updated and the function returns true.
 *
 * @param line  NUL-terminated NMEA sentence (must be non-NULL).
 * @param st    State to update in place (must be non-NULL).
 * @return true if @p st was updated, false otherwise (state unchanged).
 */
bool nmea_parse_line(const char *line, gps_state *st);

#ifdef __cplusplus
}
#endif
