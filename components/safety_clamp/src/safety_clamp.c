#include "safety_clamp.h"

#include <stdint.h>

uint32_t clamp_pwm_us(uint32_t value_us, PwmWindow window)
{
    /* SI-3 hard clamp must be fail-safe in every build, including release
     * (NDEBUG). An inverted/invalid window (min_us > max_us) can arrive from
     * untrusted sources (NVS / ESC calibration) and must never panic nor leak
     * an out-of-range value. Deterministically normalise the bounds first so
     * the clamp always operates on a valid [lo, hi] sub-range. */
    uint32_t lo = window.min_us;
    uint32_t hi = window.max_us;
    if (lo > hi) {
        lo = window.max_us;
        hi = window.min_us;
    }

    if (value_us < lo) {
        return lo;
    }
    if (value_us > hi) {
        return hi;
    }
    return value_us;
}
