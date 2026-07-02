#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure decision for the app "hold" (anchor-in-place) fix grab (Unit 3).
 *
 * On a hold request the control loop samples its OWN current GPS fix and, only
 * when that fix is usable, latches it as the SRC_GOTO target (anchor = own
 * position, R1/R2). "Usable" = a fresh, real fix whose coordinates fall inside the
 * valid geographic range. The atomic sample (one gps_get_state) happens in the
 * loop; this pure core owns only the accept/reject decision so it is host-testable
 * with oracle power (no IDF / no HAL: grep-checkable).
 *
 * The geographic range check mirrors goto_target_valid (the HTTP-path oracle for
 * an untrusted app target). It is duplicated here rather than shared because that
 * oracle lives in web_panel, which REQUIRES control_loop; depending on it back
 * would be a circular dependency. A real on-board fix is always in range, so the
 * guard only rejects garbage.
 */

/* Inclusive valid geographic range, degrees * 1e7 (mirrors goto_target.h). */
#define GOTO_GRAB_LAT_E7_MIN (-900000000)
#define GOTO_GRAB_LAT_E7_MAX (900000000)
#define GOTO_GRAB_LON_E7_MIN (-1800000000)
#define GOTO_GRAB_LON_E7_MAX (1800000000)

/** Outcome of a hold fix-grab: whether to engage and the target to latch. */
typedef struct {
    bool engage;    /* true: latch the sampled fix as the anchor target this cycle */
    int32_t lat_e7; /* latitude to latch (passthrough of the sampled fix) */
    int32_t lon_e7; /* longitude to latch (passthrough of the sampled fix) */
} goto_grab_decision;

/**
 * Decide whether a hold request may engage an anchor from the current fix.
 *
 * @param fresh   GPS freshness predicate this cycle.
 * @param fix     Real fix quality (>0) this cycle (guards the seed-fresh window).
 * @param lat_e7  Sampled latitude, degrees * 1e7.
 * @param lon_e7  Sampled longitude, degrees * 1e7.
 * @return engage = fresh && fix && in-range; lat/lon echo the sampled fix.
 */
goto_grab_decision goto_grab_decide(bool fresh, bool fix, int32_t lat_e7,
                                    int32_t lon_e7);

#ifdef __cplusplus
}
#endif
