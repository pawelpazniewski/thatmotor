#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inclusive PWM pulse-width window in microseconds.
 *
 * Represents the hard safety boundary for any value sent to an actuator.
 * `min_us` and `max_us` are both inclusive. Callers should provide
 * `min_us <= max_us`; if the bounds are inverted, clamp_pwm_us normalises
 * them deterministically (see its contract) instead of failing.
 */
typedef struct {
    uint32_t min_us;
    uint32_t max_us;
} PwmWindow;

/**
 * Unconditionally clamp a pulse width to the inclusive window [min_us, max_us].
 *
 * This is the SI-3 hard clamp: it is applied to every value that reaches an
 * output, with no branch that can bypass it. Values below min snap to min,
 * values above max snap to max, values inside the window pass unchanged.
 * Boundaries are inclusive.
 *
 * Fail-safe in every build configuration (including release / NDEBUG): if the
 * window is inverted (min_us > max_us) the bounds are normalised by swapping
 * them, so the return value is always within the valid sub-range
 * [min(bounds), max(bounds)]. The function never panics and never returns an
 * out-of-range value, regardless of the window's origin.
 *
 * @param value_us  Requested pulse width in microseconds.
 * @param window    Safety window; bounds are normalised if min_us > max_us.
 * @return          Clamped pulse width within the normalised inclusive window.
 */
uint32_t clamp_pwm_us(uint32_t value_us, PwmWindow window);

#ifdef __cplusplus
}
#endif
