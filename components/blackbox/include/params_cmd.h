#pragma once

#include <stddef.h>

#include "settings_model.h"
#include "state_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure (framework-agnostic) decision core for the `params get` / `params set`
 * console commands. It parses and validates the operator's arguments and maps
 * them onto the spot-lock regulator fields of settings_params, reusing the
 * authoritative settings_validate range gate (the SAME gate the HTTP params API
 * uses) so there is one source of truth for the ranges — no duplicated bounds.
 *
 * The console HAL stays thin: it reads the active params, calls
 * params_cmd_decide_set, and on ACCEPT hands the validated result to
 * control_loop_post_pending (SI-6: applied only while DISARMED, single-writer).
 * No ESP-IDF dependency (no esp_ or driver includes), so it is host-testable.
 */

/* Safe minimum buffer for params_cmd_format_get (four `field=value\n` lines). */
#define PARAMS_CMD_GET_MAX 128U

/** Outcome of parsing + validating a `params set <field> <value>` pair. */
typedef enum {
    PARAMS_CMD_SET_ACCEPT = 0,         /* valid; *staged holds params to post */
    PARAMS_CMD_SET_ERR_ARG,            /* NULL argument */
    PARAMS_CMD_SET_ERR_UNKNOWN_FIELD,  /* field name not a spot-lock setting */
    PARAMS_CMD_SET_ERR_BAD_VALUE,      /* value not a non-negative u16 integer */
    PARAMS_CMD_SET_ERR_OUT_OF_RANGE,   /* parsed but outside the field's range */
} params_cmd_set_outcome;

/** Whether a staged set applies now or waits for DISARMED (SI-6 gate). */
typedef enum {
    PARAMS_CMD_APPLIED = 0, /* controller is DISARMED: applies on the next cycle */
    PARAMS_CMD_STAGED = 1,  /* not DISARMED: staged, applies when disarmed */
} params_cmd_apply_status;

/**
 * Parse a `params set <field> <value>` pair, apply it onto a copy of `current`,
 * and run the authoritative settings_validate gate. On ACCEPT, *staged holds the
 * fully validated params the caller passes to control_loop_post_pending. On any
 * error, *staged is left untouched and staging must not happen.
 *
 * Recognised fields: deadband_m, max_throttle_pct, throttle_gain, servo_gain.
 *
 * @param current  Active params to base the change on (must be non-NULL).
 * @param field    Field name argument (must be non-NULL).
 * @param value    Value argument, a decimal u16 string (must be non-NULL).
 * @param staged   Destination for the validated params (must be non-NULL).
 * @return PARAMS_CMD_SET_ACCEPT, or the matching error outcome.
 */
params_cmd_set_outcome params_cmd_decide_set(const settings_params *current,
                                             const char *field,
                                             const char *value,
                                             settings_params *staged);

/**
 * Format the current spot-lock regulator settings as `field=value` lines (one
 * per line, trailing newline on each) for `params get`.
 *
 * @param params   Params to print (must be non-NULL).
 * @param out      Destination buffer (must be non-NULL).
 * @param out_len  Capacity of out; must be >= the formatted length + 1.
 * @return Number of characters written (excluding NUL), or 0 on NULL / too small.
 */
size_t params_cmd_format_get(const settings_params *params, char *out,
                             size_t out_len);

/**
 * SI-6 gate: whether a set applies immediately (DISARMED) or is staged until the
 * controller disarms. The console reports this so the operator knows a change
 * made while ARMED takes effect only after disarming (it is never dropped).
 *
 * @param state  Current controller state.
 * @return PARAMS_CMD_APPLIED when DISARMED, otherwise PARAMS_CMD_STAGED.
 */
params_cmd_apply_status params_cmd_apply_when(sm_state state);

#ifdef __cplusplus
}
#endif
