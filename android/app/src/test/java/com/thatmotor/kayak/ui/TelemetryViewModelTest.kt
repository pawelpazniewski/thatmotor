package com.thatmotor.kayak.ui

import com.thatmotor.kayak.MainDispatcherRule
import com.thatmotor.kayak.data.ApiError
import com.thatmotor.kayak.data.Command
import com.thatmotor.kayak.domain.ConnectionState
import com.thatmotor.kayak.domain.MotorState
import com.thatmotor.kayak.net.CommandResult
import com.thatmotor.kayak.net.CommandSender
import com.thatmotor.kayak.repository.TelemetrySource
import com.thatmotor.kayak.repository.TelemetryUiState
import com.thatmotor.kayak.telemetryFrame
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

/**
 * ViewModel command dispatch: sending a command publishes the mapped
 * [CommandFeedback]. Only the EXTERNAL command transport is faked
 * ([FakeCommandSender]); the telemetry source is a deterministic fake
 * ([FakeTelemetrySource]) so the availability guard sees a known link/state instead
 * of a live socket.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class TelemetryViewModelTest {

    @get:Rule
    val mainDispatcherRule = MainDispatcherRule()

    /** A live link with a DISARMED frame: ARM and DEPLOY are available. */
    private fun liveDisarmedSource(): FakeTelemetrySource = FakeTelemetrySource(
        TelemetryUiState(
            connection = ConnectionState.Live,
            latestFrame = telemetryFrame(state = MotorState.DISARMED.code),
        ),
    )

    @Test
    fun `successful command publishes success feedback`() = runTest {
        // Arrange: live DISARMED link → ARM is allowed.
        val sender = FakeCommandSender(CommandResult.Success)
        val viewModel = TelemetryViewModel(liveDisarmedSource(), sender)

        // Act
        viewModel.sendCommand(Command.ARM)
        advanceUntilIdle()

        // Assert
        val feedback = viewModel.feedback.value
        assertTrue(feedback is CommandFeedback.Success)
        assertEquals(Command.ARM, sender.lastCommand)
    }

    @Test
    fun `rejected command publishes rejection feedback without crashing`() = runTest {
        // Arrange: the controller refuses DEPLOY (allowed from DISARMED) at the wire.
        val sender = FakeCommandSender(
            CommandResult.Rejected(
                code = ApiError.CODE_NOT_DISARMED,
                message = "Settings can only be changed while DISARMED",
            ),
        )
        val viewModel = TelemetryViewModel(liveDisarmedSource(), sender)

        // Act
        viewModel.sendCommand(Command.DEPLOY)
        advanceUntilIdle()

        // Assert
        val feedback = viewModel.feedback.value
        assertTrue(feedback is CommandFeedback.Rejected)
        assertEquals(
            "Settings can only be changed while DISARMED",
            (feedback as CommandFeedback.Rejected).message,
        )
    }

    @Test
    fun `command on a stale link is vetoed before reaching the transport`() = runTest {
        // Arrange: confirm-dialog window — the link dropped Live → Stale, but the
        // DISARMED frame still reads healthy. The send guard must block the POST.
        val sender = FakeCommandSender(CommandResult.Success)
        val staleSource = FakeTelemetrySource(
            TelemetryUiState(
                connection = ConnectionState.Stale,
                latestFrame = telemetryFrame(state = MotorState.DISARMED.code),
            ),
        )
        val viewModel = TelemetryViewModel(staleSource, sender)

        // Act
        viewModel.sendCommand(Command.ARM)
        advanceUntilIdle()

        // Assert: transport never called; operator sees a rejection (oracle: removing
        // the guard would let the command through and set lastCommand).
        assertNull(sender.lastCommand)
        assertTrue(viewModel.feedback.value is CommandFeedback.Rejected)
    }

    @Test
    fun `a second command while one is in flight is dropped`() = runTest {
        // Arrange: a sender that blocks until released, so the first send stays in flight.
        val sender = GatedCommandSender()
        val viewModel = TelemetryViewModel(liveDisarmedSource(), sender)

        // Act: fire twice without letting the first complete.
        viewModel.sendCommand(Command.ARM)
        viewModel.sendCommand(Command.ARM)
        assertTrue(viewModel.isSending.value)
        sender.release()
        advanceUntilIdle()

        // Assert: the in-flight guard collapsed two taps into one POST.
        assertEquals(1, sender.callCount)
        assertTrue(viewModel.feedback.value is CommandFeedback.Success)
    }

    @Test
    fun `dismissing feedback clears it`() = runTest {
        // Arrange
        val viewModel = TelemetryViewModel(liveDisarmedSource(), FakeCommandSender(CommandResult.Success))
        viewModel.sendCommand(Command.ARM)
        advanceUntilIdle()

        // Act
        viewModel.dismissFeedback()

        // Assert
        assertNull(viewModel.feedback.value)
    }

    /** Fake of the only external dependency: records the call and returns a canned result. */
    private class FakeCommandSender(private val result: CommandResult) : CommandSender {
        var lastCommand: Command? = null
            private set

        override suspend fun send(command: Command): CommandResult {
            lastCommand = command
            return result
        }
    }

    /** Command sender that stays suspended until [release] is called. */
    private class GatedCommandSender : CommandSender {
        var callCount = 0
            private set
        private val gate = MutableStateFlow(false)

        override suspend fun send(command: Command): CommandResult {
            callCount++
            gate.first { open -> open }
            return CommandResult.Success
        }

        fun release() {
            gate.value = true
        }
    }

    /** Deterministic telemetry source: emits a fixed [TelemetryUiState]; no socket. */
    private class FakeTelemetrySource(initial: TelemetryUiState) : TelemetrySource {
        private val _state = MutableStateFlow(initial)
        override val state: StateFlow<TelemetryUiState> = _state.asStateFlow()
        var started = false
            private set

        override fun start() {
            started = true
        }

        override fun stop() {
            started = false
        }
    }
}
