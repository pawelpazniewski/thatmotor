package com.thatmotor.kayak.data

import kotlinx.serialization.Serializable

/**
 * Standard API response envelope used by every `POST /api/*` endpoint of the
 * ESP32 web panel (`components/web_panel/include/api_contract.h`).
 *
 * Exactly one of [data] / [error] is populated:
 *  - success: `{ "data": <object|null>, "error": null }`
 *  - failure: `{ "data": null, "error": { "code": "...", "message": "..." } }`
 *
 * [data] is generic over the payload type; for v1 commands the payload is unused
 * (success carries `data: null`), so callers parse `ApiEnvelope<JsonElement>` or a
 * concrete payload type as needed.
 */
@Serializable
data class ApiEnvelope<T>(
    val data: T? = null,
    val error: ApiError? = null,
)

/** Discriminated error body. [code] is a stable contract string; [message] is human-readable. */
@Serializable
data class ApiError(
    val code: String,
    val message: String,
) {
    companion object {
        /** Write rejected because the controller is not DISARMED (HTTP 409). */
        const val CODE_NOT_DISARMED = "SETTINGS_WRITE_REJECTED_NOT_DISARMED"

        /** A field failed validation (HTTP 400). */
        const val CODE_VALIDATION_FAILED = "VALIDATION_FAILED"
    }
}
