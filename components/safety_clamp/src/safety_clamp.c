#include "safety_clamp.h"

#include <assert.h>

uint32_t clamp_pwm_us(uint32_t value_us, PwmWindow window)
{
    assert(window.min_us <= window.max_us);

    if (value_us < window.min_us) {
        return window.min_us;
    }
    if (value_us > window.max_us) {
        return window.max_us;
    }
    return value_us;
}
