#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MCPWM capture timer ticks at the APB clock (80 MHz) on the ESP32, i.e.
 * 12.5 ns per tick. The conversion is expressed as a numerator/denominator
 * pair (25 / 2 ns/tick) so the math stays in integer arithmetic with
 * round-to-nearest, never a float. */
#define CAP_TICK_NS_NUM 25U
#define CAP_TICK_NS_DEN 2U

/* The MCPWM capture counter is a free-running 32-bit counter that wraps at
 * 2^32. Differences between two captured timestamps are computed modulo this
 * range so a single wrap is handled transparently. */
#define CAP_COUNTER_MODULUS_BITS 32

/**
 * Convert a count of capture-timer ticks to microseconds (round-to-nearest).
 *
 * One tick is 12.5 ns (80 MHz APB clock). Pure integer math: a pulse of
 * 80000 ticks is 1000 us. No floating point, no I/O.
 *
 * @param ticks  Number of capture-timer ticks (e.g. a pulse width in ticks).
 * @return       Equivalent duration in microseconds, rounded to nearest.
 */
uint32_t cap_ticks_to_us(uint32_t ticks);

/**
 * Number of ticks elapsed between two capture timestamps, handling a single
 * wrap of the free-running 32-bit counter.
 *
 * `now` is the later timestamp, `prev` the earlier one. Because the counter is
 * unsigned 32-bit, the natural wrap-around subtraction `now - prev` already
 * yields the correct elapsed count for any single overflow; this function makes
 * that contract explicit and is the single place the overflow rule lives.
 *
 * @param now   Later capture timestamp (ticks).
 * @param prev  Earlier capture timestamp (ticks).
 * @return      Elapsed ticks in [0, 2^32), correct across one counter wrap.
 */
uint32_t cap_ticks_elapsed(uint32_t now, uint32_t prev);

/**
 * Frame period in microseconds between two consecutive rising edges.
 *
 * Computes the elapsed ticks across a possible counter wrap, then converts to
 * microseconds. Equivalent to cap_ticks_to_us(cap_ticks_elapsed(now, prev)).
 *
 * @param edge_now   Timestamp of the current rising edge (ticks).
 * @param edge_prev  Timestamp of the previous rising edge (ticks).
 * @return           Period in microseconds (round-to-nearest).
 */
uint32_t cap_period_us(uint32_t edge_now, uint32_t edge_prev);

#ifdef __cplusplus
}
#endif
