package com.thatmotor.kayak.net

import org.junit.Assert.assertEquals
import org.junit.Test

class ApConnectionStateReducerTest {

    @Test
    fun `connecting then available yields connected`() {
        // Arrange
        val afterRequest = reduceApConnectionState(ApConnectionState.Idle, ApConnectionEvent.Requested)
        // Act
        val connected = reduceApConnectionState(afterRequest, ApConnectionEvent.Available)
        // Assert
        assertEquals(ApConnectionState.Connecting, afterRequest)
        assertEquals(ApConnectionState.Connected, connected)
    }

    @Test
    fun `connected then lost yields lost then available reconnects`() {
        // Arrange
        val connected = reduceApConnectionState(ApConnectionState.Connecting, ApConnectionEvent.Available)
        // Act
        val lost = reduceApConnectionState(connected, ApConnectionEvent.Lost)
        val reconnected = reduceApConnectionState(lost, ApConnectionEvent.Available)
        // Assert
        assertEquals(ApConnectionState.Lost, lost)
        assertEquals(ApConnectionState.Connected, reconnected)
    }

    @Test
    fun `unavailable yields failed`() {
        // Act
        val failed = reduceApConnectionState(ApConnectionState.Connecting, ApConnectionEvent.Unavailable)
        // Assert
        assertEquals(ApConnectionState.Failed, failed)
    }

    @Test
    fun `lost is ignored when not live`() {
        // A stray Lost after a Failed request must not overwrite the failure.
        // (Oracle power: this test fails if the reducer naively maps Lost -> Lost.)
        // Act
        val afterFailed = reduceApConnectionState(ApConnectionState.Failed, ApConnectionEvent.Lost)
        val afterIdle = reduceApConnectionState(ApConnectionState.Idle, ApConnectionEvent.Lost)
        // Assert
        assertEquals(ApConnectionState.Failed, afterFailed)
        assertEquals(ApConnectionState.Idle, afterIdle)
    }
}
