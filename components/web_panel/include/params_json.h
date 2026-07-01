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

/*
 * Worst-case bytes params_json_serialize can emit for the full field set,
 * including the terminating NUL. Callers MUST size their buffer to at least this
 * value: cJSON_PrintPreallocated is all-or-nothing, so a buffer even one byte
 * short yields 0 (a 500), not a truncation. This constant is derived from the
 * field count so it can never silently drift below the real output the way a
 * hand-picked literal did (that under-sizing left the panel with no params).
 *
 * Per field the compact form is "<key>":<value>, = key + 2 quotes + colon +
 * value + comma. Values are uint16 ("65535", 5) or bool ("false", 5). The field
 * count is asserted against the actual tables in params_json.c, so adding a
 * field forces this bound to grow with it.
 */
#define PARAMS_JSON_FIELD_COUNT 32   /* 28 uint16 + 3 bool + schema_version */
#define PARAMS_JSON_MAX_KEY_LEN 28   /* headroom over the longest current key */
#define PARAMS_JSON_MAX_VALUE_LEN 5  /* "65535" / "false" */
#define PARAMS_JSON_SERIALIZE_MAX                                             \
    (PARAMS_JSON_FIELD_COUNT *                                               \
         (PARAMS_JSON_MAX_KEY_LEN + 4 + PARAMS_JSON_MAX_VALUE_LEN) +         \
     2 /* braces */ + 1 /* NUL */)

/**
 * Serialise params into a compact JSON object string (no envelope).
 *
 * @param params    Source params (must be non-NULL).
 * @param out       Destination buffer (must be non-NULL). Size it to at least
 *                  PARAMS_JSON_SERIALIZE_MAX so the full field set never fails.
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
