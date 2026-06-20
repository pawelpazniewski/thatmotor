package com.thatmotor.kayak.ui

import com.thatmotor.kayak.data.ApiError
import com.thatmotor.kayak.data.Command
import com.thatmotor.kayak.net.CommandResult
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Command-action decision logic: how a transport [CommandResult] becomes the
 * operator-facing [CommandFeedback]. Pure, so it is the host-tested core of Unit 7
 * (Pure ⊥ HAL). The ViewModel dispatch path is covered in [TelemetryViewModelTest].
 */
class CommandActionTest {

    @Test
    fun `success result yields a success feedback with no error`() {
        // Arrange / Act
        val feedback = commandFeedback(Command.ARM, CommandResult.Success)

        // Assert
        assertTrue(feedback is CommandFeedback.Success)
        assertEquals("Armed", (feedback as CommandFeedback.Success).message)
    }

    @Test
    fun `rejection NOT_DISARMED yields a rejection feedback and does not crash`() {
        // Arrange: the controller refuses (e.g. deploy while not disarmed).
        val result = CommandResult.Rejected(
            code = ApiError.CODE_NOT_DISARMED,
            message = "Settings can only be changed while DISARMED",
        )

        // Act
        val feedback = commandFeedback(Command.DEPLOY, result)

        // Assert: rejection surfaces the controller message, not a success/crash.
        assertTrue(feedback is CommandFeedback.Rejected)
        assertEquals(
            "Settings can only be changed while DISARMED",
            (feedback as CommandFeedback.Rejected).message,
        )
    }

    @Test
    fun `deploy success yields the deploy success message`() {
        // Arrange / Act: success branch is per-command; DEPLOY must not reuse "Armed".
        val feedback = commandFeedback(Command.DEPLOY, CommandResult.Success)

        // Assert
        assertTrue(feedback is CommandFeedback.Success)
        assertEquals("Deployed — motor raised", (feedback as CommandFeedback.Success).message)
    }

    @Test
    fun `disarm success yields the disarm success message`() {
        // Arrange / Act
        val feedback = commandFeedback(Command.DISARM, CommandResult.Success)

        // Assert
        assertTrue(feedback is CommandFeedback.Success)
        assertEquals("Disarmed", (feedback as CommandFeedback.Success).message)
    }

    @Test
    fun `stow success yields the stow success message`() {
        // Arrange / Act
        val feedback = commandFeedback(Command.STOW, CommandResult.Success)

        // Assert
        assertTrue(feedback is CommandFeedback.Success)
        assertEquals("Stowed", (feedback as CommandFeedback.Success).message)
    }

    @Test
    fun `transport error yields a generic transport feedback`() {
        // Arrange / Act: raw I/O detail must not leak to the operator banner.
        val feedback = commandFeedback(Command.DISARM, CommandResult.TransportError("ECONNREFUSED"))

        // Assert
        assertTrue(feedback is CommandFeedback.TransportError)
        assertEquals(
            "Command failed — no response from controller",
            (feedback as CommandFeedback.TransportError).message,
        )
    }
}
