package com.thatmotor.kayak.repository

import com.thatmotor.kayak.data.TelemetryFrame
import com.thatmotor.kayak.domain.ConnectionState

/**
 * What the telemetry UI renders: the connection phase plus the latest frame (null
 * until the first frame arrives). Immutable snapshot driven by
 * [TelemetryRepository].
 */
data class TelemetryUiState(
    val connection: ConnectionState = ConnectionState.Disconnected,
    val latestFrame: TelemetryFrame? = null,
)
