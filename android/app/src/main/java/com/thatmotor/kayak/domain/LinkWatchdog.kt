package com.thatmotor.kayak.domain

/**
 * Pure link-down detector. Given the timestamp of the last received telemetry
 * frame and "now", both in the SAME monotonic clock domain (Android
 * `SystemClock.elapsedRealtime()`), it decides whether the link is [LinkStatus.LIVE]
 * or [LinkStatus.STALE].
 *
 * Single clock domain + unsigned-safe subtraction is deliberate
 * (learned-patterns: recency/elapsed in one counter domain). The caller must NOT
 * mix wall-clock and monotonic timestamps here. Threshold comparison is
 * `elapsed > threshold` so a frame exactly AT the threshold is still LIVE.
 */
enum class LinkStatus { LIVE, STALE }

/** Default staleness threshold: ~500 ms without a frame = STALE (plan / kontekst). */
const val DEFAULT_STALE_THRESHOLD_MS = 500L

/**
 * @param lastFrameElapsedMs  `elapsedRealtime()` when the last frame arrived.
 * @param nowElapsedMs        current `elapsedRealtime()`.
 * @param thresholdMs         max age before a link is considered STALE.
 */
fun linkStatus(
    lastFrameElapsedMs: Long,
    nowElapsedMs: Long,
    thresholdMs: Long = DEFAULT_STALE_THRESHOLD_MS,
): LinkStatus {
    require(thresholdMs > 0) { "thresholdMs must be positive" }
    val elapsed = nowElapsedMs - lastFrameElapsedMs
    return if (elapsed > thresholdMs) LinkStatus.STALE else LinkStatus.LIVE
}
