package com.thatmotor.kayak.domain

/**
 * App-level view of the telemetry link, independent of the underlying WiFi/AP
 * state. Discriminated (coding-rules pkt 13) so the UI renders each phase
 * distinctly.
 *
 *  - [Disconnected]: no socket (initial, or AP lost / session stopped).
 *  - [Connecting]: socket opening / reconnect backoff in progress.
 *  - [Live]: a fresh frame arrived within the staleness threshold.
 *  - [Stale]: connected but no frame within the threshold (link-down suspected).
 */
sealed interface ConnectionState {
    data object Disconnected : ConnectionState
    data object Connecting : ConnectionState
    data object Live : ConnectionState
    data object Stale : ConnectionState
}
