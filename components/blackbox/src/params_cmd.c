#include "params_cmd.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "settings_validate.h"

/* The four tunable spot-lock fields, addressed by name from the console. */
typedef enum {
    FIELD_DEADBAND_M = 0,
    FIELD_MAX_THROTTLE_PCT,
    FIELD_THROTTLE_GAIN,
    FIELD_SERVO_GAIN,
} spot_lock_field;

typedef struct {
    const char *name;
    spot_lock_field field;
} field_row;

static const field_row FIELD_TABLE[] = {
    {"deadband_m", FIELD_DEADBAND_M},
    {"max_throttle_pct", FIELD_MAX_THROTTLE_PCT},
    {"throttle_gain", FIELD_THROTTLE_GAIN},
    {"servo_gain", FIELD_SERVO_GAIN},
};

#define FIELD_TABLE_COUNT (sizeof(FIELD_TABLE) / sizeof(FIELD_TABLE[0]))

/* Resolve a field name to its enum. Returns false when the name is unknown. */
static bool lookup_field(const char *name, spot_lock_field *out)
{
    for (size_t i = 0; i < FIELD_TABLE_COUNT; ++i) {
        if (strcmp(name, FIELD_TABLE[i].name) == 0) {
            *out = FIELD_TABLE[i].field;
            return true;
        }
    }
    return false;
}

/* Parse a decimal string into a u16. Rejects empty strings, any non-digit
 * character, and values above UINT16_MAX (range enforcement is a later gate). */
static bool parse_u16(const char *str, uint16_t *out)
{
    if (str[0] == '\0') {
        return false;
    }
    uint32_t acc = 0U;
    for (const char *p = str; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        acc = acc * 10U + (uint32_t)(*p - '0');
        if (acc > UINT16_MAX) {
            return false;
        }
    }
    *out = (uint16_t)acc;
    return true;
}

/* Write the parsed value onto the addressed spot-lock field of a params copy. */
static void set_field(settings_params *params, spot_lock_field field,
                      uint16_t value)
{
    switch (field) {
    case FIELD_DEADBAND_M:
        params->spot_lock_deadband_m = value;
        break;
    case FIELD_MAX_THROTTLE_PCT:
        params->spot_lock_max_throttle_pct = value;
        break;
    case FIELD_THROTTLE_GAIN:
        params->spot_lock_throttle_gain = value;
        break;
    case FIELD_SERVO_GAIN:
        params->spot_lock_servo_gain = value;
        break;
    }
}

params_cmd_set_outcome params_cmd_decide_set(const settings_params *current,
                                             const char *field,
                                             const char *value,
                                             settings_params *staged)
{
    if (current == NULL || field == NULL || value == NULL || staged == NULL) {
        return PARAMS_CMD_SET_ERR_ARG;
    }

    spot_lock_field which;
    if (!lookup_field(field, &which)) {
        return PARAMS_CMD_SET_ERR_UNKNOWN_FIELD;
    }
    uint16_t parsed;
    if (!parse_u16(value, &parsed)) {
        return PARAMS_CMD_SET_ERR_BAD_VALUE;
    }

    settings_params candidate = *current;
    set_field(&candidate, which, parsed);

    /* Authoritative range gate: the SAME settings_validate the HTTP params API
     * uses. An out-of-range value is repaired to a default and reported as
     * !settings_valid, so we reject it here instead of staging a silent change. */
    settings_validation_result vr = settings_validate(&candidate, true, staged);
    if (!vr.settings_valid) {
        return PARAMS_CMD_SET_ERR_OUT_OF_RANGE;
    }
    return PARAMS_CMD_SET_ACCEPT;
}

size_t params_cmd_format_get(const settings_params *params, char *out,
                             size_t out_len)
{
    if (params == NULL || out == NULL) {
        return 0U;
    }
    int printed = snprintf(out, out_len,
                           "deadband_m=%u\n"
                           "max_throttle_pct=%u\n"
                           "throttle_gain=%u\n"
                           "servo_gain=%u\n",
                           (unsigned)params->spot_lock_deadband_m,
                           (unsigned)params->spot_lock_max_throttle_pct,
                           (unsigned)params->spot_lock_throttle_gain,
                           (unsigned)params->spot_lock_servo_gain);
    if (printed < 0 || (size_t)printed >= out_len) {
        return 0U;
    }
    return (size_t)printed;
}

params_cmd_apply_status params_cmd_apply_when(sm_state state)
{
    return (state == SM_STATE_DISARMED) ? PARAMS_CMD_APPLIED : PARAMS_CMD_STAGED;
}
