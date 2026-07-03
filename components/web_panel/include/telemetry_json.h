#pragma once

#include <stddef.h>

#include "control_loop_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WS telemetry JSON line buffer size. Sized from the measured worst case of
 * telemetry_json_format: with all fields present and full-width signed *_e7
 * coordinates the line reaches ~724 B (a typical fixed line is ~673 B). The
 * previous 640 B overflowed silently — snprintf returned len >= size and the
 * push dropped EVERY frame (panel showed "--" in every field). Keep headroom
 * above the measured max. A host test (test_telemetry_json) asserts a max-value
 * snapshot fits, so adding a field without bumping this fails CI, not hardware. */
#define WS_TELEMETRY_JSON_MAX 1024

/**
 * Serialise a telemetry snapshot to a compact single-line JSON string (pure, no
 * esp_* / cJSON: all fields are small integers, booleans, or enums).
 *
 * @param s    Snapshot to serialise (must be non-NULL).
 * @param buf  Destination buffer (must be non-NULL).
 * @param n    Size of @p buf.
 * @return snprintf's return: the length the full JSON would occupy (excluding
 *         the NUL). A value >= n means the output was truncated and MUST NOT be
 *         sent; callers reject it. Negative on encoding error.
 */
int telemetry_json_format(const control_loop_snapshot *s, char *buf, size_t n);

#ifdef __cplusplus
}
#endif
