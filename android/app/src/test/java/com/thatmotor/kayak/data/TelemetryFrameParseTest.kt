package com.thatmotor.kayak.data

import com.thatmotor.kayak.domain.MotorState
import com.thatmotor.kayak.domain.MotorStateResult
import com.thatmotor.kayak.domain.motorStateFromCode
import com.thatmotor.kayak.loadFixture
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TelemetryFrameParseTest {

    @Test
    fun `parses full frame with correct fields and unit conversions`() {
        // Arrange
        val json = loadFixture("telemetry_full.json")

        // Act
        val frame = parseTelemetryFrame(json)

        // Assert: raw wire fields mirror firmware exactly.
        assertEquals(1, frame.state)
        assertEquals(0, frame.armReason)
        assertTrue(frame.rcValid)
        assertEquals(1500L, frame.ch1Us)
        assertEquals(-12, frame.servoTrimUs)
        assertEquals(9, frame.gpsSats)
        assertEquals(520000000, frame.gpsLatE7)
        assertEquals(210000000, frame.gpsLonE7)
        assertEquals(250, frame.gpsSpeedCms)
        assertEquals(900, frame.imuHeadingDeg10)
        assertEquals(3, frame.imuCalib)

        // Assert: unit conversions (oracle: distinct from raw, breaks if conversion removed).
        assertEquals(52.0, coordinateE7ToDegrees(frame.gpsLatE7), 1e-9)
        assertEquals(21.0, coordinateE7ToDegrees(frame.gpsLonE7), 1e-9)
        assertEquals(90.0, headingDeg10ToDegrees(frame.imuHeadingDeg10), 1e-9)
        assertEquals(2.5, speedCmsToMetersPerSecond(frame.gpsSpeedCms), 1e-9)
    }

    @Test
    fun `frame with unknown field does not break the parser`() {
        // Arrange
        val json = loadFixture("telemetry_unknown_field.json")

        // Act
        val frame = parseTelemetryFrame(json)

        // Assert
        assertEquals(2, frame.state)
        assertFalse(frame.gpsFix)
        assertFalse(frame.imuOk)
    }

    @Test
    fun `state codes 0 to 4 map to MotorState`() {
        // Arrange / Act / Assert
        val expected = listOf(
            MotorState.DISARMED,
            MotorState.ARMED,
            MotorState.FAILSAFE,
            MotorState.ESC_CALIBRATION,
            MotorState.DEPLOY,
        )
        expected.forEachIndexed { code, state ->
            val result = motorStateFromCode(code)
            assertTrue(result is MotorStateResult.Known)
            assertEquals(state, (result as MotorStateResult.Known).state)
        }
    }

    @Test
    fun `unknown state code yields error path not crash`() {
        // Arrange / Act
        val result = motorStateFromCode(99)

        // Assert
        assertTrue(result is MotorStateResult.Unknown)
        assertEquals(99, (result as MotorStateResult.Unknown).code)
    }
}
