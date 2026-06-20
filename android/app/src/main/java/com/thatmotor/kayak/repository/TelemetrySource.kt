package com.thatmotor.kayak.repository

import kotlinx.coroutines.flow.StateFlow

/**
 * The telemetry surface the UI layer depends on: a [TelemetryUiState] stream plus
 * lifecycle control. Extracted as a seam so [TelemetryViewModel] owns the source's
 * lifecycle without binding to the concrete [TelemetryRepository] (and so tests can
 * drive state deterministically with a fake instead of a live socket).
 */
interface TelemetrySource {
    /** Current connection phase plus latest frame. */
    val state: StateFlow<TelemetryUiState>

    /** Begin collecting frames and running the watchdog. Idempotent. */
    fun start()

    /** Stop all work and reset to Disconnected. */
    fun stop()
}
