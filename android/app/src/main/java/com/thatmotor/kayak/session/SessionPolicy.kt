package com.thatmotor.kayak.session

import com.thatmotor.kayak.domain.ConnectionState

/**
 * Pure decision core for the telemetry session (Unit 10), kept HAL-free so the
 * keep-alive / wake-lock policy is host-testable on the JVM (Pure ⊥ HAL). The
 * Android [TelemetryService] / [android.app.Activity] adapters only translate these
 * decisions into framework calls (window flags, `PowerManager`, foreground service).
 *
 * The screen stays on for the whole session (the operator watches the nav screen in
 * sunlight), but the partial wake lock is acquired *only* as a last resort: while the
 * screen is on the CPU is already awake, so a wake lock would needlessly drain the
 * battery. It is reserved for the screen-off case and must be released when the
 * session ends (coding-rules pkt 13 — no dangling wake locks).
 */
object SessionPolicy {

    /**
     * Whether the nav-screen window should hold `FLAG_KEEP_SCREEN_ON`. True for the
     * whole session: the screen must not dim/sleep while telemetry is shown.
     */
    fun shouldKeepScreenOn(session: SessionState): Boolean = session == SessionState.ACTIVE

    /**
     * Whether a `PARTIAL_WAKE_LOCK` should be held. Only when the session is active
     * *and* the screen is off — while the screen is on the CPU already stays awake, so
     * a wake lock is redundant battery drain. Released as soon as either condition
     * stops holding (see [SessionService] cleanup).
     */
    fun shouldHoldWakeLock(session: SessionState, isScreenOn: Boolean): Boolean =
        session == SessionState.ACTIVE && !isScreenOn

    /**
     * One-line notification status text for the given link [connection], so the
     * foreground-service notification reflects the live link without the service
     * importing any UI/Compose code.
     */
    fun notificationStatusFor(connection: ConnectionState): String = when (connection) {
        ConnectionState.Disconnected -> "Disconnected"
        ConnectionState.Connecting -> "Connecting…"
        ConnectionState.Live -> "Telemetry live"
        ConnectionState.Stale -> "Link down — reconnecting"
    }

    /**
     * Pure session state transition. Extracted from the [TelemetryService] HAL so the
     * `onStartCommand` (→ [SessionState.ACTIVE]) / `onDestroy` (→ [SessionState.STOPPED])
     * lifecycle is host-testable (learned-patterns: "wyciągaj czyste funkcje decyzyjne
     * zza HAL").
     */
    fun nextState(event: SessionEvent): SessionState = when (event) {
        SessionEvent.STARTED -> SessionState.ACTIVE
        SessionEvent.STOPPED -> SessionState.STOPPED
    }

    /**
     * Whether the foreground notification should be refreshed for the given session
     * state. The notification only exists while the session is [SessionState.ACTIVE];
     * updating it after stop would resurrect a torn-down notification.
     */
    fun shouldUpdateStatus(session: SessionState): Boolean = session == SessionState.ACTIVE
}
