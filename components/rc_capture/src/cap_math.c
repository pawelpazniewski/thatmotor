#include "cap_math.h"

#include <stdint.h>

uint32_t cap_ticks_to_us(uint32_t ticks)
{
    /* us = ticks * 12.5 ns / 1000 = ticks * 25 / (2 * 1000) = ticks / 80.
     * Keep the explicit num/den form for clarity and round-to-nearest.
     * ticks * 25 fits in uint64_t (max 2^32 * 25 << 2^64). */
    uint64_t nanoseconds_x_den =
        (uint64_t)ticks * (uint64_t)CAP_TICK_NS_NUM; /* ns * CAP_TICK_NS_DEN */
    uint64_t denominator = (uint64_t)CAP_TICK_NS_DEN * 1000U;
    return (uint32_t)((nanoseconds_x_den + denominator / 2U) / denominator);
}

uint32_t cap_ticks_elapsed(uint32_t now, uint32_t prev)
{
    /* Unsigned 32-bit subtraction wraps modulo 2^32, which is exactly the
     * elapsed-tick count across one counter overflow. */
    return now - prev;
}

uint32_t cap_period_us(uint32_t edge_now, uint32_t edge_prev)
{
    return cap_ticks_to_us(cap_ticks_elapsed(edge_now, edge_prev));
}
