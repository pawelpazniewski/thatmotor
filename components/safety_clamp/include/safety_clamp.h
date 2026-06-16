#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inclusive PWM pulse-width window in microseconds.
 *
 * Represents the hard safety boundary for any value sent to an actuator.
 * `min_us` and `max_us` are both inclusive; callers must guarantee
 * `min_us <= max_us` (validated by clamp_pwm_us via fail-fast assert).
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
 * @param value_us  Requested pulse width in microseconds.
 * @param window    Inclusive safety window. Must satisfy min_us <= max_us.
 * @return          Clamped pulse width within [window.min_us, window.max_us].
 */
uint32_t clamp_pwm_us(uint32_t value_us, PwmWindow window);

#ifdef __cplusplus
}
#endif
