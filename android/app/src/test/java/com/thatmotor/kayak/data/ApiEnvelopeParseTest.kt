package com.thatmotor.kayak.data

import com.thatmotor.kayak.loadFixture
import kotlinx.serialization.builtins.serializer
import kotlinx.serialization.json.JsonElement
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class ApiEnvelopeParseTest {

    @Test
    fun `error envelope maps code and message`() {
        // Arrange
        val json = loadFixture("envelope_error_not_disarmed.json")

        // Act
        val envelope = espJson.decodeFromString(
            ApiEnvelope.serializer(JsonElement.serializer()),
            json,
        )

        // Assert
        assertNull(envelope.data)
        assertNotNull(envelope.error)
        assertEquals(ApiError.CODE_NOT_DISARMED, envelope.error?.code)
        assertEquals("Settings can only be changed while DISARMED", envelope.error?.message)
    }

    @Test
    fun `success envelope has null error`() {
        // Arrange
        val json = loadFixture("envelope_success.json")

        // Act
        val envelope = espJson.decodeFromString(
            ApiEnvelope.serializer(String.serializer()),
            json,
        )

        // Assert
        assertNull(envelope.data)
        assertNull(envelope.error)
    }
}
