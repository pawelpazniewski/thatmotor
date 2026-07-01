#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure, framework-agnostic sensor-freshness helpers (no IDF, host-testable).
 *
 * EPOCH CONTRACT: now_ms, last_ms and prev_ms MUST all come from the SAME
 * monotonic millisecond clock domain (on target: esp_timer_get_time() / 1000
 * truncated to uint32). Elapsed time is computed as the UNSIGNED MODULAR
 * difference (now_ms - last_ms) which stays correct across the single 2^32 ms
 * (~49.7 day) counter wrap, as long as the true elapsed interval is shorter
 * than 2^32 ms. Therefore:
 *   - Do NOT mix clock domains (e.g. a capture tick counter with a ms clock).
 *   - Do NOT pre-subtract before calling; pass the RAW stamps so the wrap-safe
 *     subtraction happens here in one place.
 */

/**
 * Whether a reading taken at @p last_ms is still fresh at @p now_ms.
 *
 * Freshness is STRICT: the reading is fresh while the elapsed time is strictly
 * less than @p threshold_ms. Exactly @p threshold_ms elapsed counts as STALE
 * (false): the reading has reached the staleness boundary.
 *
 * @param now_ms        Current time in the shared ms clock domain.
 * @param last_ms       Time the last good reading was stamped (same domain).
 * @param threshold_ms  Staleness threshold; elapsed >= this -> stale.
 * @return true when (now_ms - last_ms) < threshold_ms (wrap-safe), else false.
 */
bool sensor_is_fresh(uint32_t now_ms, uint32_t last_ms, uint32_t threshold_ms);

/**
 * Choose the "last good reading" timestamp to keep after processing a reading.
 *
 * Advances the stamp to @p now_ms ONLY when the reading was valid (e.g. a GPS
 * sentence that produced a usable fix); otherwise keeps @p prev_ms unchanged so
 * an invalid/fixless reading does not reset the staleness timer. Pure decision
 * extracted from the HAL so the "refresh only on a valid reading" rule is
 * host-testable.
 *
 * @param prev_ms        Previously stored last-good timestamp.
 * @param now_ms         Current time in the shared ms clock domain.
 * @param reading_valid  Whether this reading is a valid/good one.
 * @return now_ms when @p reading_valid, otherwise prev_ms unchanged.
 */
uint32_t sensor_freshness_stamp(uint32_t prev_ms, uint32_t now_ms,
                                bool reading_valid);

#ifdef __cplusplus
}
#endif
