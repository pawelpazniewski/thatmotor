package com.thatmotor.kayak.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.thatmotor.kayak.data.Command
import com.thatmotor.kayak.data.TelemetryFrame
import com.thatmotor.kayak.domain.CommandAvailability
import com.thatmotor.kayak.domain.ConnectionState
import com.thatmotor.kayak.domain.SafetyIndicators
import com.thatmotor.kayak.domain.commandAvailability
import com.thatmotor.kayak.domain.safetyIndicators
import com.thatmotor.kayak.net.CommandSender
import com.thatmotor.kayak.repository.TelemetrySource
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Bridges the [TelemetrySource] StateFlow to Compose and dispatches operational
 * commands through [CommandSender]. Thin: all decisions (indicators, availability,
 * feedback mapping) live in host-tested pure functions; this class only wires flows
 * and coroutines (Pure ⊥ HAL).
 *
 * Owns the source lifecycle: [TelemetrySource.start] runs in [init] (the source's own
 * scope outlives a single Activity, so it survives configuration changes) and
 * [TelemetrySource.stop] in [onCleared]. State is exposed as four narrow slices ([connection], [indicators],
 * [availability], [latestFrame]) each deduplicated with `distinctUntilChanged`, so the
 * 10 Hz frame stream only recomposes the GPS/Compass cards — not the whole screen.
 *
 * @param source  telemetry source (lifecycle owned here).
 * @param commandSender  POST /api/command transport (the only external dependency).
 */
class TelemetryViewModel(
    private val source: TelemetrySource,
    private val commandSender: CommandSender,
) : ViewModel() {

    private var isSessionStopped = false

    init {
        source.start()
    }

    /** Link phase, deduplicated. Recomposes the badge/header only on phase change. */
    val connection: StateFlow<ConnectionState> = source.state
        .map { it.connection }
        .distinctUntilChanged()
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), ConnectionState.Disconnected)

    /** Safety banners, deduplicated. Recomposes the banner only when a flag flips. */
    val indicators: StateFlow<SafetyIndicators> = source.state
        .map { safetyIndicators(it.connection, it.latestFrame) }
        .distinctUntilChanged()
        .stateIn(
            viewModelScope,
            SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS),
            SafetyIndicators.noFrame(ConnectionState.Disconnected),
        )

    /** Command enablement, deduplicated. Recomposes the command bar only on change. */
    val availability: StateFlow<CommandAvailability> = source.state
        .map { commandAvailability(it.connection, it.latestFrame) }
        .distinctUntilChanged()
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), CommandAvailability.NONE)

    /** Latest frame at the full rate; only the GPS/Compass cards read this. */
    val latestFrame: StateFlow<TelemetryFrame?> = source.state
        .map { it.latestFrame }
        .distinctUntilChanged()
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS), null)

    private val _feedback = MutableStateFlow<CommandFeedback?>(null)

    /** Latest command outcome to surface to the operator; null once dismissed. */
    val feedback: StateFlow<CommandFeedback?> = _feedback.asStateFlow()

    private val _isSending = MutableStateFlow(false)

    /** True while a command POST is in flight; the command bar disables itself. */
    val isSending: StateFlow<Boolean> = _isSending.asStateFlow()

    /**
     * Send [command] and publish its [CommandFeedback]. Never throws.
     *
     * Two guards beyond the disabled-button UI (defense-in-depth for physical hardware):
     *  - re-validate [availability] at send time — the confirm dialog may have stayed
     *    open while the link dropped (Live → Stale) or the boat state changed, so a
     *    button that was enabled when tapped may no longer be valid;
     *  - drop the call if one is already in flight, preventing a double-tap from firing
     *    several parallel POSTs (coding-rules pkt 13).
     */
    fun sendCommand(command: Command) {
        if (_isSending.value) return
        // Re-derive from the live source state (not the WhileSubscribed `availability`
        // flow, whose value is only fresh while collected) so the guard holds even if
        // the UI is momentarily not subscribed.
        val current = source.state.value
        val available = commandAvailability(current.connection, current.latestFrame)
        if (!available.isEnabled(command)) {
            _feedback.value = CommandFeedback.Rejected("Command unavailable in the current state")
            return
        }
        _isSending.value = true
        viewModelScope.launch {
            try {
                val result = commandSender.send(command)
                _feedback.value = commandFeedback(command, result)
            } finally {
                _isSending.value = false
            }
        }
    }

    /** Dismiss the current command feedback (e.g. after the snackbar is shown). */
    fun dismissFeedback() {
        _feedback.value = null
    }

    /**
     * Stop the telemetry source. Idempotent — the foreground service's `onSessionStopped`
     * hook and [onCleared] may both fire, but the source is stopped exactly once
     * (coding-rules pkt 13 — no dangling collectors).
     */
    fun stopSession() {
        if (isSessionStopped) return
        isSessionStopped = true
        source.stop()
    }

    override fun onCleared() {
        stopSession()
        super.onCleared()
    }

    companion object {
        private const val STOP_TIMEOUT_MS = 5_000L
    }
}
