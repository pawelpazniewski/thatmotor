package com.thatmotor.kayak.data

import kotlinx.serialization.json.Json

/**
 * Shared kotlinx.serialization [Json] for the ESP32 contract.
 *
 * `ignoreUnknownKeys = true`: the firmware may add fields in a future revision;
 * the app must keep parsing older/newer frames without crashing (forward-compat,
 * coding-rules pkt 4 fail-safe).
 */
val espJson: Json = Json {
    ignoreUnknownKeys = true
}

/** Parse a `WS /ws` telemetry line into a [TelemetryFrame]. */
fun parseTelemetryFrame(line: String): TelemetryFrame =
    espJson.decodeFromString(TelemetryFrame.serializer(), line)
