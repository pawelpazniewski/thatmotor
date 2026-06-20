package com.thatmotor.kayak.domain

import com.thatmotor.kayak.data.Command
import com.thatmotor.kayak.telemetryFrame
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CommandAvailabilityTest {

    @Test
    fun `DISARMED on a live link allows ARM and DEPLOY only`() {
        // Arrange / Act
        val availability = commandAvailability(
            ConnectionState.Live,
            telemetryFrame(state = MotorState.DISARMED.code),
        )

        // Assert
        assertTrue(availability.canArm)
        assertTrue(availability.canDeploy)
        assertFalse(availability.canDisarm)
        assertFalse(availability.canStow)
    }

    @Test
    fun `ARMED on a live link allows DISARM only`() {
        // Arrange / Act
        val availability = commandAvailability(
            ConnectionState.Live,
            telemetryFrame(state = MotorState.ARMED.code),
        )

        // Assert
        assertTrue(availability.canDisarm)
        assertFalse(availability.canArm)
        assertFalse(availability.canDeploy)
        assertFalse(availability.canStow)
    }

    @Test
    fun `DEPLOY state on a live link allows STOW only`() {
        // Arrange / Act
        val availability = commandAvailability(
            ConnectionState.Live,
            telemetryFrame(state = MotorState.DEPLOY.code),
        )

        // Assert
        assertTrue(availability.canStow)
        assertFalse(availability.canArm)
    }

    @Test
    fun `a stale link disables every command even with a healthy frame`() {
        // Arrange: DISARMED frame would normally enable ARM/DEPLOY — the dead link
        // must veto it (oracle: a command over a dead link cannot be confirmed).
        val availability = commandAvailability(
            ConnectionState.Stale,
            telemetryFrame(state = MotorState.DISARMED.code),
        )

        // Assert
        Command.entries.forEach { command -> assertFalse(availability.isEnabled(command)) }
    }

    @Test
    fun `no frame disables every command`() {
        // Arrange / Act
        val availability = commandAvailability(ConnectionState.Live, frame = null)

        // Assert
        Command.entries.forEach { command -> assertFalse(availability.isEnabled(command)) }
    }
}
