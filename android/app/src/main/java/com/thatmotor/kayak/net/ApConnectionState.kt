package com.thatmotor.kayak.net

/**
 * State of the bound connection to the ESP32 SoftAP.
 *
 * Discriminated union (coding-rules pkt 10) — no boolean flags. Driven purely by
 * [ConnectivityManager.NetworkCallback] events via [reduceApConnectionState].
 */
sealed interface ApConnectionState {
    /** No connection requested yet. */
    data object Idle : ApConnectionState

    /** Network requested; waiting for the framework to provide/refuse it. */
    data object Connecting : ApConnectionState

    /** Bound to the AP network; HTTP/WS traffic can flow to 192.168.4.1. */
    data object Connected : ApConnectionState

    /** Previously connected network was lost; a reconnect may follow. */
    data object Lost : ApConnectionState

    /** Framework could not satisfy the request (e.g. wrong SSID, user declined). */
    data object Failed : ApConnectionState
}
