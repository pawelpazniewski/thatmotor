#include "blackbox_ring.h"

uint32_t blackbox_ring_slot(uint32_t seq, uint32_t capacity)
{
    if (capacity == 0U) {
        return 0U;
    }
    return seq % capacity;
}

uint32_t blackbox_ring_offset(uint32_t seq, uint32_t capacity,
                              uint32_t record_size)
{
    return blackbox_ring_slot(seq, capacity) * record_size;
}

bool blackbox_ring_needs_erase(uint32_t seq, uint32_t capacity,
                               uint32_t records_per_sector)
{
    if (records_per_sector == 0U) {
        return false;
    }
    uint32_t slot = blackbox_ring_slot(seq, capacity);
    return (slot % records_per_sector) == 0U;
}

bool blackbox_ring_seq_after(uint32_t a, uint32_t b)
{
    /* Wrap-safe: the signed modular difference is positive when `a` follows
     * `b`, even across the u32 wrap (e.g. a=0, b=UINT32_MAX -> +1 -> after). */
    return (int32_t)(a - b) > 0;
}

uint32_t blackbox_ring_oldest_seq(uint32_t newest_seq, uint32_t count)
{
    if (count == 0U) {
        return newest_seq;
    }
    /* Unsigned modular subtraction wraps correctly through 0. */
    return newest_seq - (count - 1U);
}
