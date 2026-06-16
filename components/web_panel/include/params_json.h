#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * settings_params <-> JSON mapping (Unit 10) using cJSON.
 *
 * One place owns the field name <-> struct field correspondence so params_api
 * stays small. Parsing starts from the current active params and overlays only
 * the recognised numeric/boolean keys present in the request, so a partial POST
 * updates a subset; unknown keys are ignored. Serialisation emits every field.
 */

/**
 * Serialise params into a compact JSON object string (no envelope).
 *
 * @param params    Source params (must be non-NULL).
 * @param out       Destination buffer (must be non-NULL).
 * @param out_size  Size of out in bytes.
 * @return Number of bytes written (excluding NUL), or 0 on failure/overflow.
 */
size_t params_json_serialize(const settings_params *params, char *out,
                             size_t out_size);

/**
 * Parse a JSON object string, overlaying recognised keys onto *params.
 *
 * @param json    NUL-terminated JSON object (may be NULL/empty -> false).
 * @param params  In/out params: seeded by the caller (active params), updated
 *                in place for each recognised key (must be non-NULL).
 * @return true when the body parsed as a JSON object; false on malformed JSON.
 */
bool params_json_parse(const char *json, settings_params *params);

#ifdef __cplusplus
}
#endif
