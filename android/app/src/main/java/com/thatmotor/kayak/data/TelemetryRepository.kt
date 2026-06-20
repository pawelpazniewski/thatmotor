package com.thatmotor.kayak.data

import android.os.SystemClock
import com.thatmotor.kayak.domain.ConnectionState
import com.thatmotor.kayak.domain.DEFAULT_STALE_THRESHOLD_MS
import com.thatmotor.kayak.domain.LinkStatus
import com.thatmotor.kayak.domain.linkStatus
import com.thatmotor.kayak.domain.reconnectDelayMs
import com.thatmotor.kayak.net.TelemetrySocket
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Drives [TelemetryUiState] from the [TelemetrySocket] frame stream, applying the
 * link-down watchdog and a reconnect backoff.
 *
 * Thin HAL adapter: the decisions (LIVE/STALE threshold, backoff growth) live in
 * the host-tested pure core ([linkStatus], [reconnectDelayMs]); this class only
 * wires the coroutine plumbing and the monotonic clock ([SystemClock.elapsedRealtime]).
 *
 * Two cooperating loops:
 *  - the collect loop receives frames and stamps [lastFrameElapsedMs];
 *  - the watchdog loop periodically re-evaluates [linkStatus] so a SILENT link
 *    (socket up, frames stopped) flips to [ConnectionState.Stale] within ~1 tick
 *    even though no frame triggers a transition.
 *
 * Reconnect is paused while the AP is reported lost via [pauseReconnect]; resume
 * with [resumeReconnect] (driven by the AP connection layer in a later phase).
 */
class TelemetryRepository(
    private val scope: CoroutineScope,
    private val socket: TelemetrySocket,
    private val staleThresholdMs: Long = DEFAULT_STALE_THRESHOLD_MS,
    private val now: () -> Long = SystemClock::elapsedRealtime,
) {
    private val _state = MutableStateFlow(TelemetryUiState())
    val state: StateFlow<TelemetryUiState> = _state.asStateFlow()

    @Volatile
    private var reconnectPaused = false

    @Volatile
    private var lastFrameElapsedMs: Long = 0

    @Volatile
    private var hasFrame = false

    private var collectJob: Job? = null
    private var watchdogJob: Job? = null

    /** Start collecting frames (with reconnect backoff) and the staleness watchdog. Idempotent. */
    fun start() {
        if (collectJob?.isActive == true) return
        collectJob = scope.launch {
            var attempt = 0
            while (isActive) {
                attempt = runConnectAttempt(attempt)
            }
        }
        watchdogJob = scope.launch {
            while (isActive) {
                delay(WATCHDOG_TICK_MS)
                tickWatchdog()
            }
        }
    }

    /** Stop both loops and reset to Disconnected. */
    fun stop() {
        collectJob?.cancel()
        watchdogJob?.cancel()
        collectJob = null
        watchdogJob = null
        hasFrame = false
        _state.value = _state.value.copy(connection = ConnectionState.Disconnected)
    }

    /** Pause reconnect attempts (e.g. AP Lost). */
    fun pauseReconnect() {
        reconnectPaused = true
    }

    /** Resume reconnect attempts (e.g. AP reconnected). */
    fun resumeReconnect() {
        reconnectPaused = false
    }

    /** One connect/collect cycle. Returns the next reconnect attempt counter. */
    private suspend fun runConnectAttempt(attempt: Int): Int {
        if (reconnectPaused) {
            _state.value = _state.value.copy(connection = ConnectionState.Disconnected)
            delay(RECONNECT_PAUSE_POLL_MS)
            return attempt
        }
        _state.value = _state.value.copy(connection = ConnectionState.Connecting)
        var nextAttempt = attempt
        runCatching {
            socket.frames()
                .onEach { frame ->
                    nextAttempt = 0
                    onFrame(frame)
                }
                .catch { /* surface as a reconnect, not a crash */ }
                .collect {}
        }
        // Stream ended (closed/failed): mark stale and back off before retrying.
        _state.value = _state.value.copy(connection = ConnectionState.Stale)
        if (reconnectPaused) return nextAttempt
        delay(reconnectDelayMs(nextAttempt))
        return nextAttempt + 1
    }

    private fun tickWatchdog() {
        if (!hasFrame) return
        val status = linkStatus(lastFrameElapsedMs, now(), staleThresholdMs)
        val current = _state.value
        // Only demote a live link; never overwrite Connecting/Disconnected here.
        if (current.connection == ConnectionState.Live && status == LinkStatus.STALE) {
            _state.value = current.copy(connection = ConnectionState.Stale)
        }
    }

    private fun onFrame(frame: TelemetryFrame) {
        lastFrameElapsedMs = now()
        hasFrame = true
        _state.value = TelemetryUiState(
            connection = ConnectionState.Live,
            latestFrame = frame,
        )
    }

    companion object {
        private const val RECONNECT_PAUSE_POLL_MS = 250L
        private const val WATCHDOG_TICK_MS = 200L
    }
}
