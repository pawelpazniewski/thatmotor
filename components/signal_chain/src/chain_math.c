#include "chain_math.h"

#include <stdbool.h>
#include <stdint.h>

#include "signal_chain.h"

#define FULL_SCALE SIGNAL_NORMALIZED_FULL_SCALE

/* Scale a value spanning `span` input units onto the full-scale half-range,
 * rounding to nearest. `span` is guaranteed > 0 by the callers below. */
static int32_t scale_half(int32_t delta, int32_t span)
{
    int64_t scaled = (int64_t)delta * FULL_SCALE;
    int32_t sign = scaled < 0 ? -1 : 1;
    int64_t rounded = (scaled * sign + span / 2) / span;
    return (int32_t)(sign * rounded);
}

int32_t normalize_us(uint32_t raw_us, uint16_t min_us, uint16_t mid_us,
                     uint16_t max_us)
{
    if (raw_us <= min_us) {
        return -FULL_SCALE;
    }
    if (raw_us >= max_us) {
        return FULL_SCALE;
    }
    if (raw_us == mid_us) {
        return 0;
    }
    if (raw_us < mid_us) {
        int32_t span = (int32_t)mid_us - (int32_t)min_us;
        return scale_half((int32_t)raw_us - (int32_t)mid_us, span);
    }
    int32_t span = (int32_t)max_us - (int32_t)mid_us;
    return scale_half((int32_t)raw_us - (int32_t)mid_us, span);
}

int32_t apply_deadband(int32_t value, int32_t deadband)
{
    int32_t magnitude = value < 0 ? -value : value;
    if (magnitude <= deadband) {
        return 0;
    }
    return value;
}

int32_t apply_reverse(int32_t value, bool reverse)
{
    return reverse ? -value : value;
}

int32_t deadband_us_to_normalized(uint16_t deadband_us, uint16_t min_us,
                                  uint16_t mid_us, uint16_t max_us)
{
    int32_t low_span = (int32_t)mid_us - (int32_t)min_us;
    int32_t high_span = (int32_t)max_us - (int32_t)mid_us;
    int32_t span = low_span < high_span ? low_span : high_span;
    if (span <= 0) {
        return 0;
    }
    return scale_half((int32_t)deadband_us, span);
}

uint32_t map_normalized_to_us(int32_t value, uint32_t min_us, uint32_t center_us,
                              uint32_t max_us)
{
    if (value == 0) {
        return center_us;
    }
    if (value < 0) {
        int64_t span = (int64_t)center_us - (int64_t)min_us;
        int64_t offset = (span * (-value) + FULL_SCALE / 2) / FULL_SCALE;
        return (uint32_t)((int64_t)center_us - offset);
    }
    int64_t span = (int64_t)max_us - (int64_t)center_us;
    int64_t offset = (span * value + FULL_SCALE / 2) / FULL_SCALE;
    return (uint32_t)((int64_t)center_us + offset);
}
