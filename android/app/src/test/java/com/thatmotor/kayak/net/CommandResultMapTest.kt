package com.thatmotor.kayak.net

import com.thatmotor.kayak.data.ApiError
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CommandResultMapTest {

    @Test
    fun `200 with null error is Success`() {
        // Arrange / Act
        val result = mapCommandResult(httpStatus = 200, error = null)

        // Assert
        assertEquals(CommandResult.Success, result)
    }

    @Test
    fun `409 NOT_DISARMED is Rejected with reason`() {
        // Arrange
        val error = ApiError(
            code = ApiError.CODE_NOT_DISARMED,
            message = "Settings can only be changed while DISARMED",
        )

        // Act
        val result = mapCommandResult(httpStatus = 409, error = error)

        // Assert
        assertTrue(result is CommandResult.Rejected)
        result as CommandResult.Rejected
        assertEquals(ApiError.CODE_NOT_DISARMED, result.code)
        assertEquals("Settings can only be changed while DISARMED", result.message)
    }

    @Test
    fun `400 VALIDATION_FAILED is Rejected with reason`() {
        // Arrange
        val error = ApiError(
            code = ApiError.CODE_VALIDATION_FAILED,
            message = "One or more parameters failed validation",
        )

        // Act
        val result = mapCommandResult(httpStatus = 400, error = error)

        // Assert
        assertTrue(result is CommandResult.Rejected)
        assertEquals(ApiError.CODE_VALIDATION_FAILED, (result as CommandResult.Rejected).code)
    }

    @Test
    fun `non-2xx without an error envelope is a TransportError`() {
        // Arrange / Act
        val result = mapCommandResult(httpStatus = 500, error = null)

        // Assert
        assertTrue(result is CommandResult.TransportError)
    }
}
