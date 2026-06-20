package com.thatmotor.kayak.domain

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ReconnectBackoffTest {

    @Test
    fun `backoff grows by doubling from the initial delay`() {
        // Arrange / Act / Assert: 250 -> 500 -> 1000 (plan schedule).
        assertEquals(250L, reconnectDelayMs(attempt = 0))
        assertEquals(500L, reconnectDelayMs(attempt = 1))
        assertEquals(1_000L, reconnectDelayMs(attempt = 2))
    }

    @Test
    fun `backoff is capped at the maximum`() {
        // Arrange / Act: attempt 3 would be 2000 (== cap), attempt 10 must not exceed cap.
        // Oracle: without the cap, attempt 10 would be 250 * 2^10 = 256000.
        val atCap = reconnectDelayMs(attempt = 3)
        val farPastCap = reconnectDelayMs(attempt = 10)

        // Assert
        assertEquals(2_000L, atCap)
        assertEquals(MAX_BACKOFF_MS, farPastCap)
        assertTrue(farPastCap <= MAX_BACKOFF_MS)
    }
}
