package com.thatmotor.kayak.session

import com.thatmotor.kayak.domain.ConnectionState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SessionPolicyTest {

    @Test
    fun `keep screen on while session is active`() {
        // Arrange / Act / Assert
        assertTrue(SessionPolicy.shouldKeepScreenOn(SessionState.ACTIVE))
    }

    @Test
    fun `do not keep screen on once the session is stopped`() {
        // Oracle: a policy that always returned true would fail this case.
        assertFalse(SessionPolicy.shouldKeepScreenOn(SessionState.STOPPED))
    }

    @Test
    fun `hold wake lock only when active and the screen is off`() {
        // Arrange / Act / Assert — the single combination that should hold a lock.
        assertTrue(SessionPolicy.shouldHoldWakeLock(SessionState.ACTIVE, isScreenOn = false))
    }

    @Test
    fun `no wake lock while the screen is on even when active`() {
        // Oracle: with the screen on the CPU is already awake; a lock would only drain
        // battery. A policy ignoring `isScreenOn` would fail here.
        assertFalse(SessionPolicy.shouldHoldWakeLock(SessionState.ACTIVE, isScreenOn = true))
    }

    @Test
    fun `no wake lock once the session is stopped regardless of screen`() {
        // Both screen states must release the lock after stop (no dangling lock).
        assertFalse(SessionPolicy.shouldHoldWakeLock(SessionState.STOPPED, isScreenOn = false))
        assertFalse(SessionPolicy.shouldHoldWakeLock(SessionState.STOPPED, isScreenOn = true))
    }

    @Test
    fun `notification status reflects each connection phase`() {
        // Each phase maps to a distinct operator-facing string.
        assertEquals("Disconnected", SessionPolicy.notificationStatusFor(ConnectionState.Disconnected))
        assertEquals("Connecting…", SessionPolicy.notificationStatusFor(ConnectionState.Connecting))
        assertEquals("Telemetry live", SessionPolicy.notificationStatusFor(ConnectionState.Live))
        assertEquals("Link down — reconnecting", SessionPolicy.notificationStatusFor(ConnectionState.Stale))
    }

    @Test
    fun `live and stale statuses differ`() {
        // Oracle: guards against a regression that collapses phases to one string.
        assertTrue(
            SessionPolicy.notificationStatusFor(ConnectionState.Live) !=
                SessionPolicy.notificationStatusFor(ConnectionState.Stale),
        )
    }

    @Test
    fun `started event activates the session`() {
        // onStartCommand transition: STOPPED/initial -> ACTIVE.
        assertEquals(SessionState.ACTIVE, SessionPolicy.nextState(SessionEvent.STARTED))
    }

    @Test
    fun `stopped event ends the session`() {
        // onDestroy transition: ACTIVE -> STOPPED.
        // Oracle: a reducer that always returned ACTIVE would fail here.
        assertEquals(SessionState.STOPPED, SessionPolicy.nextState(SessionEvent.STOPPED))
    }

    @Test
    fun `status updates only while the session is active`() {
        // The foreground notification exists only while ACTIVE; updating after stop
        // would resurrect a torn-down notification.
        assertTrue(SessionPolicy.shouldUpdateStatus(SessionState.ACTIVE))
    }

    @Test
    fun `no status updates once the session is stopped`() {
        // Oracle: a guard that always allowed updates would fail this case.
        assertFalse(SessionPolicy.shouldUpdateStatus(SessionState.STOPPED))
    }
}
