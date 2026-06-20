package com.thatmotor.kayak.domain

/**
 * Pure reconnect backoff schedule for the telemetry socket: 250 ms → 1 s → max
 * 2 s (plan Unit 5). Doubling, clamped to [maxMs]. Kept HAL-free so the growth +
 * cap is host-testable around the saturation boundary (oracle power).
 *
 * @param attempt  zero-based reconnect attempt (0 = first delay).
 */
fun reconnectDelayMs(
    attempt: Int,
    initialMs: Long = INITIAL_BACKOFF_MS,
    maxMs: Long = MAX_BACKOFF_MS,
): Long {
    require(attempt >= 0) { "attempt must be >= 0" }
    require(initialMs in 1..maxMs) { "initialMs must be in 1..maxMs" }
    // Double `attempt` times, but stop multiplying once we reach the cap to avoid
    // overflow on large attempt counts.
    var delay = initialMs
    repeat(attempt) {
        if (delay >= maxMs) return maxMs
        delay *= 2
    }
    return delay.coerceAtMost(maxMs)
}

const val INITIAL_BACKOFF_MS = 250L
const val MAX_BACKOFF_MS = 2_000L
