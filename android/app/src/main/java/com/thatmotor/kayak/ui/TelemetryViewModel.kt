package com.thatmotor.kayak.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.thatmotor.kayak.data.Command
import com.thatmotor.kayak.domain.CommandAvailability
import com.thatmotor.kayak.domain.SafetyIndicators
import com.thatmotor.kayak.domain.commandAvailability
import com.thatmotor.kayak.domain.safetyIndicators
import com.thatmotor.kayak.net.CommandSender
import com.thatmotor.kayak.repository.TelemetryRepository
import com.thatmotor.kayak.repository.TelemetryUiState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Everything [TelemetryScreen] renders: the raw repository state plus the derived,
 * host-tested safety indicators and command availability. Composed once per frame so
 * the UI does not re-run the pure derivations on every recomposition.
 */
data class TelemetryScreenState(
    val telemetry: TelemetryUiState = TelemetryUiState(),
    val indicators: SafetyIndicators = SafetyIndicators.noFrame(TelemetryUiState().connection),
    val availability: CommandAvailability = CommandAvailability.NONE,
)

/**
 * Bridges the [TelemetryRepository] StateFlow to Compose and dispatches operational
 * commands through [CommandApi]. Thin: all decisions (indicators, availability,
 * feedback mapping) live in host-tested pure functions; this class only wires flows
 * and coroutines (Pure ⊥ HAL).
 *
 * @param repository  telemetry source (already started by the host/Activity).
 * @param commandSender  POST /api/command transport (the only external dependency).
 */
class TelemetryViewModel(
    private val repository: TelemetryRepository,
    private val commandSender: CommandSender,
) : ViewModel() {

    val state: StateFlow<TelemetryScreenState> = repository.state
        .map { telemetry ->
            TelemetryScreenState(
                telemetry = telemetry,
                indicators = safetyIndicators(telemetry.connection, telemetry.latestFrame),
                availability = commandAvailability(telemetry.connection, telemetry.latestFrame),
            )
        }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(STOP_TIMEOUT_MS),
            initialValue = TelemetryScreenState(),
        )

    private val _feedback = MutableStateFlow<CommandFeedback?>(null)

    /** Latest command outcome to surface to the operator; null once dismissed. */
    val feedback: StateFlow<CommandFeedback?> = _feedback.asStateFlow()

    /** Send [command] and publish its [CommandFeedback]. Never throws. */
    fun sendCommand(command: Command) {
        viewModelScope.launch {
            val result = commandSender.send(command)
            _feedback.value = commandFeedback(command, result)
        }
    }

    /** Dismiss the current command feedback (e.g. after the snackbar is shown). */
    fun dismissFeedback() {
        _feedback.value = null
    }

    companion object {
        private const val STOP_TIMEOUT_MS = 5_000L
    }
}
