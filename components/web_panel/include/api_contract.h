#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * API response envelope builder (Unit 10), pure logic.
 *
 * Every params API response uses the same shape: { data, error }. Exactly one
 * of the two is populated:
 *   - success:        { "data": <object|null>, "error": null }
 *   - failure:        { "data": null, "error": { "code": "...", "message": ... } }
 *
 * This module owns the discriminated error-code -> string/message mapping and
 * the envelope serialisation into a caller-provided buffer. It has NO IDF / no
 * cJSON dependency so the contract is fully host-testable. The data payload is
 * an already-serialised JSON fragment supplied by the caller (e.g. the params
 * object from params_api), or NULL for an empty success.
 */

/** Discriminated API error codes. Stable string codes are part of the contract. */
typedef enum {
    API_OK = 0,                              /* success, no error */
    API_ERR_VALIDATION_FAILED = 1,           /* a field failed validation */
    API_ERR_NOT_DISARMED = 2,                /* write rejected: state != DISARMED */
    API_ERR_BAD_REQUEST = 3,                 /* malformed/unparseable JSON body */
    API_ERR_INTERNAL = 4,                    /* unexpected server-side failure */
} api_error_code;

/* Stable string code emitted for a write rejected because the controller is not
 * DISARMED (R17/SI-6). Surfaced verbatim in error.code and matched by the panel. */
#define API_CODE_NOT_DISARMED "SETTINGS_WRITE_REJECTED_NOT_DISARMED"

/**
 * Map an error code to its stable string code (the value of error.code).
 *
 * @param code  Error code (must not be API_OK).
 * @return Stable, non-NULL string code. API_OK yields "OK" (not used in output).
 */
const char *api_error_code_str(api_error_code code);

/**
 * Map an error code to a stable human-readable default message.
 *
 * @param code  Error code (must not be API_OK).
 * @return Stable, non-NULL default message.
 */
const char *api_error_default_message(api_error_code code);

/**
 * Build a success envelope: { "data": <data_json|null>, "error": null }.
 *
 * @param data_json  Already-serialised JSON value for the data field, or NULL
 *                   for { "data": null, "error": null }. Not validated.
 * @param out        Destination buffer (must be non-NULL).
 * @param out_size   Size of out in bytes.
 * @return Number of bytes written (excluding the NUL), or 0 if out_size is too
 *         small (out is left NUL-terminated when out_size >= 1).
 */
size_t api_build_success(const char *data_json, char *out, size_t out_size);

/**
 * Build an error envelope: { "data": null, "error": { "code", "message" } }.
 *
 * @param code     Error code (must not be API_OK; API_OK is treated as
 *                 API_ERR_INTERNAL to keep the envelope well-formed).
 * @param message  Human-readable message, or NULL to use the default for code.
 * @param out      Destination buffer (must be non-NULL).
 * @param out_size Size of out in bytes.
 * @return Number of bytes written (excluding the NUL), or 0 if out_size is too
 *         small (out is left NUL-terminated when out_size >= 1).
 */
size_t api_build_error(api_error_code code, const char *message, char *out,
                       size_t out_size);

#ifdef __cplusplus
}
#endif
