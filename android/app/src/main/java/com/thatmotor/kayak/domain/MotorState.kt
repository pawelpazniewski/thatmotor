package com.thatmotor.kayak.domain

/**
 * The control state-machine state, mirroring the firmware `sm_state` enum
 * (`components/state_machine/include/state_machine.h`): 0..4.
 */
enum class MotorState(val code: Int) {
    DISARMED(0),
    ARMED(1),
    FAILSAFE(2),
    ESC_CALIBRATION(3),
    DEPLOY(4),
}

/**
 * Why arming is blocked this cycle, mirroring the firmware `sm_arm_reason` enum:
 * 0..4. [READY] (0) means a fresh arm intent would arm.
 */
enum class ArmReason(val code: Int) {
    READY(0),
    NO_RC(1),
    THROTTLE_NOT_NEUTRAL(2),
    CALIBRATING(3),
    SETTINGS_APPLYING(4),
}

/**
 * Result of mapping a raw wire value to a domain enum: a recognised value, or the
 * unknown code preserved for diagnostics. Discriminated so callers handle the
 * unknown path explicitly instead of crashing on an out-of-range value
 * (coding-rules pkt 4: fail-safe, not fail-crash).
 */
sealed interface MotorStateResult {
    data class Known(val state: MotorState) : MotorStateResult
    data class Unknown(val code: Int) : MotorStateResult
}

/** Map a raw `state` code (0..4) to [MotorState]; unknown codes are preserved, not thrown. */
fun motorStateFromCode(code: Int): MotorStateResult {
    val match = MotorState.entries.firstOrNull { it.code == code }
    return if (match != null) MotorStateResult.Known(match) else MotorStateResult.Unknown(code)
}

/** Map a raw `arm_reason` code (0..4) to [ArmReason]; an unknown code yields null. */
fun armReasonFromCode(code: Int): ArmReason? = ArmReason.entries.firstOrNull { it.code == code }
