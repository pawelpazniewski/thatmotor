package com.thatmotor.kayak.domain

import com.thatmotor.kayak.data.TelemetryFrame

/**
 * The operational safety indicators the UI renders as banners, derived purely from
 * the link [ConnectionState] and the latest [TelemetryFrame] (may be null before the
 * first frame). No Android/HAL dependency so the decision is host-testable
 * (Pure ⊥ HAL, learned-patterns).
 *
 * Each flag is independent — several can be active at once (e.g. a stale link while
 * the last known frame was ARMED). The UI decides banner ordering/priority; this
 * layer only states the facts.
 *
 *  - [linkDown]: the telemetry link is not Live (Disconnected/Connecting/Stale).
 *    Driven by [ConnectionState] alone so it asserts even when the last frame still
 *    reads healthy (a frozen-but-old frame must not look safe).
 *  - [failsafe]: the controller reported the FAILSAFE state (RC lost / drive stopped).
 *  - [armed]: the drive is ARMED (motor may spin) — distinct from DEPLOY.
 *  - [uncalibrated]: settings/ESC not calibrated; arming/operation is unreliable.
 */
data class SafetyIndicators(
    val linkDown: Boolean,
    val failsafe: Boolean,
    val armed: Boolean,
    val uncalibrated: Boolean,
) {
    companion object {
        /**
         * Indicators when no frame has ever arrived: only the link state is known.
         * Frame-derived flags stay false (we have no evidence either way) — the
         * dominant [linkDown] banner already tells the operator the link is down.
         */
        fun noFrame(connection: ConnectionState): SafetyIndicators = SafetyIndicators(
            linkDown = connection != ConnectionState.Live,
            failsafe = false,
            armed = false,
            uncalibrated = false,
        )
    }
}

/**
 * Derive the [SafetyIndicators] from the current link state and latest frame.
 *
 * @param connection  app-level link phase.
 * @param frame       most recent telemetry frame, or null if none received yet.
 */
fun safetyIndicators(connection: ConnectionState, frame: TelemetryFrame?): SafetyIndicators {
    if (frame == null) return SafetyIndicators.noFrame(connection)
    val state = motorStateFromCode(frame.state)
    val isFailsafe = state is MotorStateResult.Known && state.state == MotorState.FAILSAFE
    val isArmed = state is MotorStateResult.Known && state.state == MotorState.ARMED
    return SafetyIndicators(
        linkDown = connection != ConnectionState.Live,
        failsafe = isFailsafe,
        armed = isArmed,
        uncalibrated = !frame.calibrated,
    )
}
