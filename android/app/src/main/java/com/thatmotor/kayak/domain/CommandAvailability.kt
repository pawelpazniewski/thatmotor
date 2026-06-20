package com.thatmotor.kayak.domain

import com.thatmotor.kayak.data.Command
import com.thatmotor.kayak.data.TelemetryFrame

/**
 * Which operational commands the operator may issue right now, derived purely from
 * the link state and latest frame. Pure (Pure ⊥ HAL) so the UI's enable/disable
 * logic is host-testable.
 *
 * Rules mirror the firmware state machine and the web panel's affordances:
 *  - Nothing is allowed unless the link is [ConnectionState.Live] and a frame exists
 *    (a command sent over a dead link cannot be confirmed — see plan: arm disabled
 *    when link down).
 *  - ARM is offered only from DISARMED; DISARM only when ARMED or FAILSAFE.
 *  - DEPLOY is offered only from DISARMED (firmware: deploy raises the motor from a
 *    stopped/disarmed drive); STOW only from DEPLOY.
 */
data class CommandAvailability(
    val canArm: Boolean,
    val canDisarm: Boolean,
    val canDeploy: Boolean,
    val canStow: Boolean,
) {
    /** Whether the given [command] is currently enabled. */
    fun isEnabled(command: Command): Boolean = when (command) {
        Command.ARM -> canArm
        Command.DISARM -> canDisarm
        Command.DEPLOY -> canDeploy
        Command.STOW -> canStow
    }

    companion object {
        /** All commands disabled (no live link / no frame). */
        val NONE = CommandAvailability(
            canArm = false,
            canDisarm = false,
            canDeploy = false,
            canStow = false,
        )
    }
}

/**
 * Compute the [CommandAvailability] for the current [connection] and [frame].
 *
 * @param connection  app-level link phase.
 * @param frame       most recent telemetry frame, or null if none received yet.
 */
fun commandAvailability(connection: ConnectionState, frame: TelemetryFrame?): CommandAvailability {
    if (connection != ConnectionState.Live || frame == null) return CommandAvailability.NONE
    val state = motorStateFromCode(frame.state)
    if (state !is MotorStateResult.Known) return CommandAvailability.NONE
    return when (state.state) {
        MotorState.DISARMED -> CommandAvailability(
            canArm = true,
            canDisarm = false,
            canDeploy = true,
            canStow = false,
        )
        MotorState.ARMED -> CommandAvailability(
            canArm = false,
            canDisarm = true,
            canDeploy = false,
            canStow = false,
        )
        MotorState.FAILSAFE -> CommandAvailability(
            canArm = false,
            canDisarm = true,
            canDeploy = false,
            canStow = false,
        )
        MotorState.DEPLOY -> CommandAvailability(
            canArm = false,
            canDisarm = false,
            canDeploy = false,
            canStow = true,
        )
        MotorState.ESC_CALIBRATION -> CommandAvailability.NONE
    }
}
