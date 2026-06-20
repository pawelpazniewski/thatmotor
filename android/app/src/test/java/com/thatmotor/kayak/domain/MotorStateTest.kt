package com.thatmotor.kayak.domain

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class MotorStateTest {

    @Test
    fun `arm reason 0 maps to READY`() {
        // Arrange / Act
        val reason = armReasonFromCode(0)

        // Assert
        assertEquals(ArmReason.READY, reason)
    }

    @Test
    fun `arm reasons 1 to 4 map to their enum entries`() {
        // Arrange: the full recognised range (firmware sm_arm_reason 0..4).
        // Oracle: each code must resolve to the entry whose `code` matches, so a
        // swapped/renamed enum row would fail here.
        val expected = mapOf(
            1 to ArmReason.NO_RC,
            2 to ArmReason.THROTTLE_NOT_NEUTRAL,
            3 to ArmReason.CALIBRATING,
            4 to ArmReason.SETTINGS_APPLYING,
        )

        // Act / Assert
        expected.forEach { (code, reason) ->
            assertEquals("code $code", reason, armReasonFromCode(code))
        }
    }

    @Test
    fun `unknown arm reason code yields null`() {
        // Arrange: 99 is outside the firmware 0..4 range (documented error path).
        // Act
        val reason = armReasonFromCode(99)

        // Assert: error path returns null instead of crashing.
        assertNull(reason)
    }
}
