package com.thatmotor.kayak.domain

import org.junit.Assert.assertEquals
import org.junit.Test

class LinkWatchdogTest {

    private val threshold = 500L

    @Test
    fun `frame exactly at threshold is still LIVE`() {
        // Arrange: elapsed == threshold (boundary). Oracle: a naive `>=` would
        // wrongly flip to STALE here, so this pins the `>` comparison.
        val last = 1_000L
        val now = last + threshold

        // Act
        val status = linkStatus(last, now, threshold)

        // Assert
        assertEquals(LinkStatus.LIVE, status)
    }

    @Test
    fun `one ms past threshold is STALE`() {
        // Arrange
        val last = 1_000L
        val now = last + threshold + 1

        // Act
        val status = linkStatus(last, now, threshold)

        // Assert
        assertEquals(LinkStatus.STALE, status)
    }

    @Test
    fun `a fresh frame after stale returns LIVE`() {
        // Arrange: previously stale (large gap), then a new frame arrives "now".
        val stale = linkStatus(lastFrameElapsedMs = 1_000L, nowElapsedMs = 5_000L, thresholdMs = threshold)
        val freshLast = 5_000L
        val freshNow = 5_010L

        // Act
        val recovered = linkStatus(freshLast, freshNow, threshold)

        // Assert
        assertEquals(LinkStatus.STALE, stale)
        assertEquals(LinkStatus.LIVE, recovered)
    }
}
