#pragma once

#include <stdbool.h>

#include "api_contract.h"
#include "state_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Params write-gate decision (Unit 10), pure logic.
 *
 * The headline contract for a settings POST (R17/SI-6) is: a write is accepted
 * ONLY while DISARMED, and only after every field re-validates. This module owns
 * that decision as a pure function so the full decision chain (not just the leaf
 * error string in api_contract) is host-testable. params_api stays the thin HAL
 * glue: parse JSON, re-validate, then delegate the accept/reject decision here.
 *
 * Precedence matches the firmware boundary: the DISARMED gate is checked BEFORE
 * field validity, so a write while ARMED is rejected as not-DISARMED regardless
 * of field validity. It has NO IDF / no cJSON dependency.
 */

/** Discriminated outcome of the write-gate decision. */
typedef enum {
    PARAMS_WRITE_ACCEPT = 0,             /* DISARMED + valid -> stage pending */
    PARAMS_WRITE_REJECT_NOT_DISARMED = 1,/* state != DISARMED -> 409 */
    PARAMS_WRITE_REJECT_INVALID = 2,     /* a field failed re-validation -> 400 */
} params_write_decision;

/** Decision plus the api_contract code/HTTP status to surface for it. */
typedef struct {
    params_write_decision decision; /* discriminated outcome */
    api_error_code code;            /* API_OK on accept, else the error code */
    int http_status;                /* 200 / 409 / 400 */
} params_write_outcome;

/**
 * Decide whether a parsed params write is accepted (pure).
 *
 * @param state         Current controller state.
 * @param fields_valid  Whether every field passed server-side re-validation.
 * @return The discriminated decision with its api_contract code + HTTP status:
 *   - state != DISARMED            -> REJECT_NOT_DISARMED, API_ERR_NOT_DISARMED, 409
 *   - DISARMED + invalid field     -> REJECT_INVALID, API_ERR_VALIDATION_FAILED, 400
 *   - DISARMED + all fields valid  -> ACCEPT, API_OK, 200
 */
params_write_outcome params_decide_write(sm_state state, bool fields_valid);

#ifdef __cplusplus
}
#endif
