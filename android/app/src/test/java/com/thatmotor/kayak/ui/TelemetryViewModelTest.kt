package com.thatmotor.kayak.ui

import com.thatmotor.kayak.MainDispatcherRule
import com.thatmotor.kayak.data.ApiError
import com.thatmotor.kayak.data.Command
import com.thatmotor.kayak.net.CommandResult
import com.thatmotor.kayak.net.CommandSender
import com.thatmotor.kayak.net.EspHttpClient
import com.thatmotor.kayak.net.TelemetrySocket
import com.thatmotor.kayak.repository.TelemetryRepository
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
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
 * ([FakeCommandSender]); the repository is real but never collected here (the
 * command path does not depend on the frame stream).
 */
@OptIn(ExperimentalCoroutinesApi::class)
class TelemetryViewModelTest {

    @get:Rule
    val mainDispatcherRule = MainDispatcherRule()

    private fun newRepository(scope: CoroutineScope): TelemetryRepository {
        val socket = TelemetrySocket(EspHttpClient.build(network = null))
        // Inject a fixed clock so no android.os.SystemClock reference is touched on the JVM.
        return TelemetryRepository(scope = scope, socket = socket, now = { 0L })
    }

    @Test
    fun `successful command publishes success feedback`() = runTest {
        // Arrange
        val sender = FakeCommandSender(CommandResult.Success)
        val viewModel = TelemetryViewModel(newRepository(this), sender)

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
        // Arrange
        val sender = FakeCommandSender(
            CommandResult.Rejected(
                code = ApiError.CODE_NOT_DISARMED,
                message = "Settings can only be changed while DISARMED",
            ),
        )
        val viewModel = TelemetryViewModel(newRepository(this), sender)

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
    fun `dismissing feedback clears it`() = runTest {
        // Arrange
        val viewModel = TelemetryViewModel(newRepository(this), FakeCommandSender(CommandResult.Success))
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
}
