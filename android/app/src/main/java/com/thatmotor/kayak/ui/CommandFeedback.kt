package com.thatmotor.kayak.ui

import com.thatmotor.kayak.data.Command
import com.thatmotor.kayak.net.CommandResult

/**
 * User-facing outcome of an operational command, derived purely from the
 * [CommandResult] returned by the transport. Discriminated (coding-rules pkt 10) so
 * the UI styles success / rejection / transport failure distinctly, and host-testable
 * (Pure ⊥ HAL) — no Android dependency.
 */
sealed interface CommandFeedback {
    /** The command was accepted by the controller. */
    data class Success(val message: String) : CommandFeedback

    /** The controller refused the command (e.g. not DISARMED, validation). */
    data class Rejected(val message: String) : CommandFeedback

    /** The command never reached the controller (no link, I/O, bad response). */
    data class TransportError(val message: String) : CommandFeedback
}

/**
 * Map the transport [result] of sending [command] to a [CommandFeedback] message.
 *
 * Rejections surface the controller's own [CommandResult.Rejected.message] (the
 * firmware contract string is the source of truth). Transport errors are reported
 * generically to the operator (the raw detail belongs in logs, not the banner —
 * coding-rules pkt 4).
 */
fun commandFeedback(command: Command, result: CommandResult): CommandFeedback = when (result) {
    is CommandResult.Success -> CommandFeedback.Success(successMessage(command))
    is CommandResult.Rejected -> CommandFeedback.Rejected(result.message)
    is CommandResult.TransportError ->
        CommandFeedback.TransportError("Command failed — no response from controller")
}

private fun successMessage(command: Command): String = when (command) {
    Command.ARM -> "Armed"
    Command.DISARM -> "Disarmed"
    Command.DEPLOY -> "Deployed — motor raised"
    Command.STOW -> "Stowed"
}
