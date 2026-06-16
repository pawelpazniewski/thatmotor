#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Params REST API logic (Unit 10): JSON (de)serialisation + the write gate.
 *
 * The web boundary ONLY validates and stages (R17/SI-6): a POST is parsed,
 * re-validated server-side field-by-field, gated to DISARMED (else 409 with code
 * SETTINGS_WRITE_REJECTED_NOT_DISARMED), and on success posted to the control
 * loop's length-1 pending mailbox. The loop is the single writer of active
 * params. Responses always use the { data, error } envelope (api_contract).
 *
 * This module is the thin glue between http_server (transport) and the pure
 * cores (settings_validate, api_contract); it depends on cJSON + control_loop.
 */

/** Result of a params request: the HTTP status and the rendered body. */
typedef struct {
    int http_status;        /* 200 / 400 / 409 / 500 */
    size_t body_len;        /* bytes written to body (excluding NUL) */
} params_api_response;

/**
 * Serialise the current active params into a { data, error:null } envelope for
 * a GET. Never fails the request; on an internal error returns a 500 envelope.
 *
 * @param body      Destination buffer for the JSON response (must be non-NULL).
 * @param body_size Size of body in bytes.
 * @return The HTTP status and body length.
 */
params_api_response params_api_handle_get(char *body, size_t body_size);

/**
 * Handle a params POST: parse, re-validate, gate on DISARMED, and on success
 * stage the validated params to the control loop's pending mailbox. Writes the
 * { data, error } envelope into body.
 *
 *  - malformed JSON              -> 400, BAD_REQUEST
 *  - state != DISARMED           -> 409, SETTINGS_WRITE_REJECTED_NOT_DISARMED
 *  - a field fails validation    -> 400, VALIDATION_FAILED
 *  - accepted                    -> 200, { data: <params>, error: null }
 *
 * @param request_json Raw request body (NUL-terminated, may be NULL/empty).
 * @param body         Destination buffer for the JSON response (non-NULL).
 * @param body_size    Size of body in bytes.
 * @return The HTTP status and body length.
 */
params_api_response params_api_handle_post(const char *request_json, char *body,
                                           size_t body_size);

#ifdef __cplusplus
}
#endif
