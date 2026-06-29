#include "sensor_freshness.h"

bool sensor_is_fresh(uint32_t now_ms, uint32_t last_ms, uint32_t threshold_ms)
{
    /* Unsigned modular subtraction: correct across the single 2^32 ms wrap as
     * long as the true interval is < 2^32 ms (see header epoch contract). */
    return (uint32_t)(now_ms - last_ms) < threshold_ms;
}

uint32_t sensor_freshness_stamp(uint32_t prev_ms, uint32_t now_ms,
                                bool reading_valid)
{
    return reading_valid ? now_ms : prev_ms;
}
