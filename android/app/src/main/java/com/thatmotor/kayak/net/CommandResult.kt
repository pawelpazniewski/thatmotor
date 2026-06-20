package com.thatmotor.kayak.net

import com.thatmotor.kayak.data.ApiError

/**
 * Outcome of a `POST /api/command`, as seen by the UI. Discriminated so the UI
 * handles each branch explicitly (coding-rules pkt 10: discriminated unions).
 */
sealed interface CommandResult {
    /** HTTP 2xx and `error == null`. */
    data object Success : CommandResult

    /** The controller refused the command (e.g. not DISARMED, validation). */
    data class Rejected(val code: String, val message: String) : CommandResult

    /** Transport/protocol failure (no envelope, non-2xx without an error body, I/O). */
    data class TransportError(val detail: String) : CommandResult
}

/**
 * Pure mapping: (HTTP status, parsed envelope error) → [CommandResult].
 *
 * Mirrors the firmware contract: success is 2xx with a null error; a populated
 * error (regardless of the specific HTTP status, e.g. 409 NOT_DISARMED, 400
 * VALIDATION_FAILED) is a [Rejected]. A non-2xx status with no parsed error is a
 * [TransportError] (the body was not a well-formed envelope). Kept HAL-free so it
 * is host-testable (Pure ⊥ HAL).
 *
 * @param httpStatus  HTTP response status code.
 * @param error       The envelope's `error` field, or null on success.
 */
fun mapCommandResult(httpStatus: Int, error: ApiError?): CommandResult {
    val isSuccessStatus = httpStatus in 200..299
    return when {
        error != null -> CommandResult.Rejected(error.code, error.message)
        isSuccessStatus -> CommandResult.Success
        else -> CommandResult.TransportError("HTTP $httpStatus without an error envelope")
    }
}
