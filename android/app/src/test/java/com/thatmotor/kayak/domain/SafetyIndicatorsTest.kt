package com.thatmotor.kayak.domain

import com.thatmotor.kayak.telemetryFrame
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SafetyIndicatorsTest {

    @Test
    fun `state FAILSAFE activates the failsafe indicator`() {
        // Arrange: FAILSAFE = state code 2, link Live so failsafe is the only flag.
        val frame = telemetryFrame(state = MotorState.FAILSAFE.code)

        // Act
        val indicators = safetyIndicators(ConnectionState.Live, frame)

        // Assert
        assertTrue(indicators.failsafe)
        assertFalse(indicators.armed)
    }

    @Test
    fun `Stale connection activates linkDown regardless of last frame`() {
        // Arrange: last frame reads perfectly healthy (DISARMED, calibrated) — only
        // the Stale link must drive linkDown (oracle: a frozen healthy frame is unsafe).
        val healthyFrame = telemetryFrame(state = MotorState.DISARMED.code, calibrated = true)

        // Act
        val indicators = safetyIndicators(ConnectionState.Stale, healthyFrame)

        // Assert
        assertTrue(indicators.linkDown)
    }

    @Test
    fun `Live connection does not activate linkDown`() {
        // Arrange / Act
        val indicators = safetyIndicators(ConnectionState.Live, telemetryFrame())

        // Assert: breaks if linkDown ignored connection and defaulted true.
        assertFalse(indicators.linkDown)
    }

    @Test
    fun `calibrated false activates the UNCALIBRATED indicator`() {
        // Arrange
        val frame = telemetryFrame(calibrated = false)

        // Act
        val indicators = safetyIndicators(ConnectionState.Live, frame)

        // Assert
        assertTrue(indicators.uncalibrated)
    }

    @Test
    fun `calibrated true does not activate UNCALIBRATED`() {
        // Arrange / Act
        val indicators = safetyIndicators(ConnectionState.Live, telemetryFrame(calibrated = true))

        // Assert: oracle for the calibrated path (would fail if always uncalibrated).
        assertFalse(indicators.uncalibrated)
    }

    @Test
    fun `state ARMED activates the armed indicator`() {
        // Arrange / Act
        val indicators = safetyIndicators(ConnectionState.Live, telemetryFrame(state = MotorState.ARMED.code))

        // Assert
        assertTrue(indicators.armed)
        assertFalse(indicators.failsafe)
    }

    @Test
    fun `no frame reports linkDown from connection only`() {
        // Arrange / Act: Disconnected with no frame.
        val indicators = safetyIndicators(ConnectionState.Disconnected, frame = null)

        // Assert
        assertTrue(indicators.linkDown)
        assertFalse(indicators.failsafe)
        assertFalse(indicators.uncalibrated)
    }
}
