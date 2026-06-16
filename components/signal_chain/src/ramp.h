#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Rate-limited approach toward a target, one control cycle at a time.
 *
 * These helpers move a current value toward a target by at most a fixed step
 * per call and NEVER overshoot it: once the remaining distance is smaller than
 * the step the target is reached exactly. They are pure (no state beyond the
 * passed-in current value), so the caller owns the ramp state across cycles.
 */

/**
 * Throttle soft-start / soft-stop ramp with independent up/down rates.
 *
 * Moves `current` toward `target` by at most `rate_up` when increasing the
 * magnitude away from zero is not the model: rates are chosen by direction of
 * change in signed command space. Rising command (target > current) uses
 * `rate_up`; falling command (target < current) uses `rate_down`. The step is
 * clamped to the remaining distance so the result never passes the target.
 *
 * @param current   Current ramped value.
 * @param target    Desired value to approach.
 * @param rate_up   Max increase per cycle (> 0).
 * @param rate_down Max decrease per cycle (> 0).
 * @return New ramped value, one step closer to target (never past it).
 */
int32_t ramp_step(int32_t current, int32_t target, int32_t rate_up,
                  int32_t rate_down);

/**
 * Symmetric slew limiter: moves `current` toward `target` by at most `rate`
 * per cycle, in either direction, never overshooting the target.
 *
 * @param current Current value.
 * @param target  Desired value to approach.
 * @param rate    Max change per cycle, same magnitude up and down (> 0).
 * @return New value, one step closer to target (never past it).
 */
int32_t slew_step(int32_t current, int32_t target, int32_t rate);

#ifdef __cplusplus
}
#endif
