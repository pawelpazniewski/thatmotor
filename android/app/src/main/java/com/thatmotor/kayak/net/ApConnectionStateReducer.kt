package com.thatmotor.kayak.net

/**
 * Events emitted by [android.net.ConnectivityManager.NetworkCallback] while
 * connecting to the ESP32 SoftAP. Decoupled from Android types so the reducer is
 * a pure function testable on the JVM (Pure ⊥ HAL, learned-patterns).
 */
sealed interface ApConnectionEvent {
    /** requestNetwork() was issued. */
    data object Requested : ApConnectionEvent

    /** onAvailable(network): network is up and (after binding) usable. */
    data object Available : ApConnectionEvent

    /** onLost(network): a connected network went away. */
    data object Lost : ApConnectionEvent

    /** onUnavailable(): the request could not be satisfied. */
    data object Unavailable : ApConnectionEvent
}

/**
 * Pure reducer: (current state, event) → next state.
 *
 * The framework can re-fire [ApConnectionEvent.Available] after a [Lost] (the OS
 * reconnects), so [Available] always maps to [ApConnectionState.Connected].
 * [ApConnectionEvent.Lost] only means something while we were live
 * ([Connecting]/[Connected]); a stray [Lost] in any other state is ignored to
 * avoid masking a [Failed]/[Idle] result. [Unavailable] is terminal-failure for
 * the current request.
 */
fun reduceApConnectionState(
    current: ApConnectionState,
    event: ApConnectionEvent,
): ApConnectionState = when (event) {
    ApConnectionEvent.Requested -> ApConnectionState.Connecting
    ApConnectionEvent.Available -> ApConnectionState.Connected
    ApConnectionEvent.Unavailable -> ApConnectionState.Failed
    ApConnectionEvent.Lost -> when (current) {
        ApConnectionState.Connecting, ApConnectionState.Connected -> ApConnectionState.Lost
        else -> current
    }
}
