#include "settings_validate.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings_model.h"
#include "settings_ranges.h"

/* Clamp-to-default helper: if value is outside [min,max], take the default and
 * flag that a repair happened. Returns the value to use. */
static uint16_t field_or_default(uint16_t value, uint16_t min, uint16_t max,
                                 uint16_t fallback, bool *repaired)
{
    if (value < min || value > max) {
        *repaired = true;
        return fallback;
    }
    return value;
}

/* Signed clamp-to-default helper for symmetric +/-max_abs fields: if value is
 * outside [-max_abs, +max_abs], take the default and flag the repair. */
static int16_t signed_field_or_default(int16_t value, int16_t max_abs,
                                       int16_t fallback, bool *repaired)
{
    if (value < -max_abs || value > max_abs) {
        *repaired = true;
        return fallback;
    }
    return value;
}

/* Per-field range checks. Repairs out-of-range fields in *p in place against
 * the defaults in *def, setting *repaired when any field is replaced. */
static void validate_fields(settings_params *p, const settings_params *def,
                            bool *repaired)
{
    p->rc_min_us = field_or_default(p->rc_min_us, RC_US_MIN, RC_US_MAX,
                                    def->rc_min_us, repaired);
    p->rc_mid_us = field_or_default(p->rc_mid_us, RC_US_MIN, RC_US_MAX,
                                    def->rc_mid_us, repaired);
    p->rc_max_us = field_or_default(p->rc_max_us, RC_US_MIN, RC_US_MAX,
                                    def->rc_max_us, repaired);

    p->servo_slew_us_per_cycle =
        field_or_default(p->servo_slew_us_per_cycle, SERVO_SLEW_MIN,
                         SERVO_SLEW_MAX, def->servo_slew_us_per_cycle, repaired);
    p->servo_min_us = field_or_default(p->servo_min_us, SERVO_US_MIN,
                                       SERVO_US_MAX, def->servo_min_us, repaired);
    p->servo_max_us = field_or_default(p->servo_max_us, SERVO_US_MIN,
                                       SERVO_US_MAX, def->servo_max_us, repaired);
    p->steer_deadband_us = field_or_default(p->steer_deadband_us, 0U,
                                            DEADBAND_MAX, def->steer_deadband_us,
                                            repaired);
    /* Signed servo trim: clamp to [-SERVO_TRIM_MAX_US, +SERVO_TRIM_MAX_US]. */
    p->servo_trim_us = signed_field_or_default(p->servo_trim_us,
                                               SERVO_TRIM_MAX_US,
                                               def->servo_trim_us, repaired);

    p->esc_ramp_up_us_per_cycle =
        field_or_default(p->esc_ramp_up_us_per_cycle, ESC_RAMP_MIN, ESC_RAMP_MAX,
                         def->esc_ramp_up_us_per_cycle, repaired);
    p->esc_ramp_down_us_per_cycle =
        field_or_default(p->esc_ramp_down_us_per_cycle, ESC_RAMP_MIN,
                         ESC_RAMP_MAX, def->esc_ramp_down_us_per_cycle, repaired);
    p->throttle_deadband_us =
        field_or_default(p->throttle_deadband_us, 0U, DEADBAND_MAX,
                         def->throttle_deadband_us, repaired);
    p->max_throttle_fwd_pct =
        field_or_default(p->max_throttle_fwd_pct, MAX_THROTTLE_PCT_MIN,
                         MAX_THROTTLE_PCT_MAX, def->max_throttle_fwd_pct,
                         repaired);
    p->max_throttle_rev_pct =
        field_or_default(p->max_throttle_rev_pct, MAX_THROTTLE_PCT_MIN,
                         MAX_THROTTLE_PCT_MAX, def->max_throttle_rev_pct,
                         repaired);

    p->esc_neutral_us = field_or_default(p->esc_neutral_us, ESC_NEUTRAL_MIN,
                                         ESC_NEUTRAL_MAX, def->esc_neutral_us,
                                         repaired);
    p->esc_neutral_band_us =
        field_or_default(p->esc_neutral_band_us, ESC_NEUTRAL_BAND_MIN,
                         ESC_NEUTRAL_BAND_MAX, def->esc_neutral_band_us,
                         repaired);
    p->esc_forward_min_us = field_or_default(p->esc_forward_min_us, ESC_US_MIN,
                                             ESC_US_MAX, def->esc_forward_min_us,
                                             repaired);
    p->esc_forward_max_us = field_or_default(p->esc_forward_max_us, ESC_US_MIN,
                                             ESC_US_MAX, def->esc_forward_max_us,
                                             repaired);
    p->esc_reverse_min_us = field_or_default(p->esc_reverse_min_us, ESC_US_MIN,
                                             ESC_US_MAX, def->esc_reverse_min_us,
                                             repaired);
    p->esc_reverse_max_us = field_or_default(p->esc_reverse_max_us, ESC_US_MIN,
                                             ESC_US_MAX, def->esc_reverse_max_us,
                                             repaired);

    p->failsafe_timeout_ms =
        field_or_default(p->failsafe_timeout_ms, FAILSAFE_TIMEOUT_MS_MIN,
                         FAILSAFE_TIMEOUT_MS_MAX, def->failsafe_timeout_ms,
                         repaired);
    p->reverse_neutral_dwell_ms = field_or_default(
        p->reverse_neutral_dwell_ms, REVERSE_NEUTRAL_DWELL_MS_MIN,
        REVERSE_NEUTRAL_DWELL_MS_MAX, def->reverse_neutral_dwell_ms, repaired);

    /* CH4 mode switch: the threshold must sit inside the RC pulse band; the
     * enabled flag is a bool and has no range to check. */
    p->ch4_switch_threshold_us =
        field_or_default(p->ch4_switch_threshold_us, RC_US_MIN, RC_US_MAX,
                         def->ch4_switch_threshold_us, repaired);

    /* DEPLOY mode: servo target in the full servo band; click window in ms. */
    p->deploy_servo_us =
        field_or_default(p->deploy_servo_us, DEPLOY_SERVO_US_MIN,
                         DEPLOY_SERVO_US_MAX, def->deploy_servo_us, repaired);
    p->click_window_ms =
        field_or_default(p->click_window_ms, CLICK_WINDOW_MS_MIN,
                         CLICK_WINDOW_MS_MAX, def->click_window_ms, repaired);
}

/* The ESC map (map_normalized_to_us in the throttle chain) treats
 * esc_reverse_max_us as the -full endpoint, esc_neutral_us as centre and
 * esc_forward_max_us as the +full endpoint. They must increase in that order or
 * the map silently inverts direction (full forward maps below neutral); the
 * hard clamp keeps the output in band but the direction is wrong. On violation,
 * restore all three from defaults so the resulting map is monotonic. */
static void validate_esc_map_monotonic(settings_params *p,
                                       const settings_params *def,
                                       bool *repaired)
{
    bool is_monotonic = p->esc_reverse_max_us < p->esc_neutral_us &&
                        p->esc_neutral_us < p->esc_forward_max_us;
    if (is_monotonic) {
        return;
    }
    p->esc_reverse_max_us = def->esc_reverse_max_us;
    p->esc_neutral_us = def->esc_neutral_us;
    p->esc_forward_max_us = def->esc_forward_max_us;
    *repaired = true;
}

/* Cross-field invariants. On violation, restore the whole related group from
 * defaults (so the group stays internally consistent) and flag a repair. */
static void validate_cross_fields(settings_params *p,
                                  const settings_params *def, bool *repaired)
{
    /* RC calibration must be monotonic: min < mid < max. */
    if (!(p->rc_min_us < p->rc_mid_us && p->rc_mid_us < p->rc_max_us)) {
        p->rc_min_us = def->rc_min_us;
        p->rc_mid_us = def->rc_mid_us;
        p->rc_max_us = def->rc_max_us;
        *repaired = true;
    }

    /* Servo endpoints ascending. */
    if (p->servo_min_us >= p->servo_max_us) {
        p->servo_min_us = def->servo_min_us;
        p->servo_max_us = def->servo_max_us;
        *repaired = true;
    }

    /* Forward ESC band ascends away from neutral. */
    if (p->esc_forward_min_us > p->esc_forward_max_us) {
        p->esc_forward_min_us = def->esc_forward_min_us;
        p->esc_forward_max_us = def->esc_forward_max_us;
        *repaired = true;
    }

    /* Reverse ESC band descends away from neutral (reverse_min nearer neutral
     * than reverse_max). */
    if (p->esc_reverse_min_us < p->esc_reverse_max_us) {
        p->esc_reverse_min_us = def->esc_reverse_min_us;
        p->esc_reverse_max_us = def->esc_reverse_max_us;
        *repaired = true;
    }

    validate_esc_map_monotonic(p, def, repaired);
}

static settings_validation_result make_defaults_result(settings_params *out,
                                                       bool nvs_error)
{
    settings_load_defaults(out);
    settings_validation_result result = {
        .source = SETTINGS_SOURCE_DEFAULTS,
        .settings_valid = false,
        .calibrated = false,
        .defaults_used = true,
        .nvs_error = nvs_error,
    };
    return result;
}

settings_validation_result settings_validate(const settings_params *stored,
                                             bool has_stored,
                                             settings_params *out)
{
    /* No usable blob (empty/corrupt/version-mismatch, decided by the loader):
     * pure conservative defaults. The nvs_error distinction (blob present but
     * unreadable) is owned by the NVS layer in a later unit, so it is not set
     * here. */
    if (!has_stored || stored == NULL) {
        return make_defaults_result(out, false);
    }

    settings_params defaults;
    settings_load_defaults(&defaults);

    *out = *stored;
    out->schema_version = SETTINGS_SCHEMA_VERSION;

    bool repaired = false;
    validate_fields(out, &defaults, &repaired);
    validate_cross_fields(out, &defaults, &repaired);

    settings_validation_result result = {
        .source = repaired ? SETTINGS_SOURCE_MIXED_RECOVERED
                           : SETTINGS_SOURCE_NVS,
        .settings_valid = !repaired,
        .calibrated = !repaired,
        .defaults_used = repaired,
        .nvs_error = false,
    };
    return result;
}
