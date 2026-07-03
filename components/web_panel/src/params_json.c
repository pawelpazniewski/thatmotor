#include "params_json.h"

#include <string.h>

#include "cJSON.h"

/* Table-driven uint16 field mapping: one row per JSON key <-> struct offset.
 * Keeps the (de)serialiser short and the key set in a single place. */
typedef struct {
    const char *key;
    size_t offset;
} u16_field;

#define U16_FIELD(name) {#name, offsetof(settings_params, name)}

static const u16_field U16_FIELDS[] = {
    U16_FIELD(rc_min_us),
    U16_FIELD(rc_mid_us),
    U16_FIELD(rc_max_us),
    U16_FIELD(servo_slew_us_per_cycle),
    U16_FIELD(servo_min_us),
    U16_FIELD(servo_max_us),
    U16_FIELD(steer_deadband_us),
    U16_FIELD(esc_ramp_up_us_per_cycle),
    U16_FIELD(esc_ramp_down_us_per_cycle),
    U16_FIELD(throttle_deadband_us),
    U16_FIELD(max_throttle_fwd_pct),
    U16_FIELD(max_throttle_rev_pct),
    U16_FIELD(esc_neutral_us),
    U16_FIELD(esc_neutral_band_us),
    U16_FIELD(esc_forward_min_us),
    U16_FIELD(esc_forward_max_us),
    U16_FIELD(esc_reverse_min_us),
    U16_FIELD(esc_reverse_max_us),
    U16_FIELD(failsafe_timeout_ms),
    U16_FIELD(reverse_neutral_dwell_ms),
    U16_FIELD(ch4_switch_threshold_us),
    U16_FIELD(deploy_servo_us),
    U16_FIELD(click_window_ms),
    U16_FIELD(spot_lock_deadband_m),
    U16_FIELD(spot_lock_max_throttle_pct),
    U16_FIELD(spot_lock_throttle_gain),
    U16_FIELD(spot_lock_servo_gain),
    U16_FIELD(goto_comms_timeout_ms),
    U16_FIELD(goto_slowdown_distance_m),
};

#define U16_FIELD_COUNT (sizeof(U16_FIELDS) / sizeof(U16_FIELDS[0]))

typedef struct {
    const char *key;
    size_t offset;
} bool_field;

static const bool_field BOOL_FIELDS[] = {
    {"servo_reverse", offsetof(settings_params, servo_reverse)},
    {"throttle_reverse", offsetof(settings_params, throttle_reverse)},
    {"ch4_mode_switch_enabled",
     offsetof(settings_params, ch4_mode_switch_enabled)},
};

#define BOOL_FIELD_COUNT (sizeof(BOOL_FIELDS) / sizeof(BOOL_FIELDS[0]))

/* Keep the serialise-buffer bound (params_json.h) in lockstep with the real
 * field set: +1 for schema_version. Adding a field without bumping
 * PARAMS_JSON_FIELD_COUNT (and thus the buffer) breaks the build here instead of
 * silently overflowing the panel's params response at runtime. */
_Static_assert(U16_FIELD_COUNT + BOOL_FIELD_COUNT + 1 == PARAMS_JSON_FIELD_COUNT,
               "PARAMS_JSON_FIELD_COUNT out of sync with the field tables");

static uint16_t *u16_ptr(settings_params *p, size_t offset)
{
    return (uint16_t *)((char *)p + offset);
}

static bool *bool_ptr(settings_params *p, size_t offset)
{
    return (bool *)((char *)p + offset);
}

/* Clamp a JSON number into the uint16 range before storing (per-field range
 * validation still happens downstream in settings_validate). */
static uint16_t clamp_u16(double v)
{
    if (v < 0.0) {
        return 0U;
    }
    if (v > 65535.0) {
        return 65535U;
    }
    return (uint16_t)v;
}

static void add_u16_fields(cJSON *obj, const settings_params *p)
{
    for (size_t i = 0; i < U16_FIELD_COUNT; ++i) {
        const uint16_t *field = (const uint16_t *)((const char *)p +
                                                   U16_FIELDS[i].offset);
        cJSON_AddNumberToObject(obj, U16_FIELDS[i].key, *field);
    }
}

static void add_bool_fields(cJSON *obj, const settings_params *p)
{
    for (size_t i = 0; i < BOOL_FIELD_COUNT; ++i) {
        const bool *field = (const bool *)((const char *)p +
                                           BOOL_FIELDS[i].offset);
        cJSON_AddBoolToObject(obj, BOOL_FIELDS[i].key, *field);
    }
}

size_t params_json_serialize(const settings_params *params, char *out,
                             size_t out_size)
{
    if (params == NULL || out == NULL || out_size == 0U) {
        return 0U;
    }
    cJSON *obj = cJSON_CreateObject();
    if (obj == NULL) {
        return 0U;
    }
    cJSON_AddNumberToObject(obj, "schema_version", params->schema_version);
    add_u16_fields(obj, params);
    add_bool_fields(obj, params);

    size_t written = 0U;
    if (cJSON_PrintPreallocated(obj, out, (int)out_size, false)) {
        written = strlen(out);
    }
    cJSON_Delete(obj);
    return written;
}

static void overlay_u16_fields(const cJSON *root, settings_params *params)
{
    for (size_t i = 0; i < U16_FIELD_COUNT; ++i) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(root,
                                                             U16_FIELDS[i].key);
        if (cJSON_IsNumber(item)) {
            *u16_ptr(params, U16_FIELDS[i].offset) = clamp_u16(item->valuedouble);
        }
    }
}

static void overlay_bool_fields(const cJSON *root, settings_params *params)
{
    for (size_t i = 0; i < BOOL_FIELD_COUNT; ++i) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(root,
                                                            BOOL_FIELDS[i].key);
        if (cJSON_IsBool(item)) {
            *bool_ptr(params, BOOL_FIELDS[i].offset) = cJSON_IsTrue(item);
        }
    }
}

bool params_json_parse(const char *json, settings_params *params)
{
    if (json == NULL || params == NULL || json[0] == '\0') {
        return false;
    }
    cJSON *root = cJSON_Parse(json);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    overlay_u16_fields(root, params);
    overlay_bool_fields(root, params);
    cJSON_Delete(root);
    return true;
}
