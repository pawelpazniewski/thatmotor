#include "settings_validate.h"

#include "settings_model.h"
#include "settings_ranges.h"

void settings_load_defaults(settings_params *out)
{
    /* Conservative built-in calibration (R16): centred RC, gentle ramps/slew,
     * low power limit, ESC neutral at 1500 us. Guaranteed to pass every
     * per-field and cross-field invariant in settings_validate. */
    const settings_params defaults = {
        .schema_version = SETTINGS_SCHEMA_VERSION,

        .rc_min_us = RC_MIN_DEFAULT,
        .rc_mid_us = RC_MID_DEFAULT,
        .rc_max_us = RC_MAX_DEFAULT,

        .servo_slew_us_per_cycle = SERVO_SLEW_DEFAULT,
        .servo_min_us = SERVO_MIN_DEFAULT,
        .servo_max_us = SERVO_MAX_DEFAULT,
        .steer_deadband_us = STEER_DEADBAND_DEFAULT,
        .servo_reverse = false,
        .servo_trim_us = SERVO_TRIM_DEFAULT,

        .esc_ramp_up_us_per_cycle = ESC_RAMP_UP_DEFAULT,
        .esc_ramp_down_us_per_cycle = ESC_RAMP_DOWN_DEFAULT,
        .throttle_deadband_us = THROTTLE_DEADBAND_DEFAULT,
        .max_throttle_fwd_pct = MAX_THROTTLE_FWD_PCT_DEFAULT,
        .max_throttle_rev_pct = MAX_THROTTLE_REV_PCT_DEFAULT,
        .throttle_reverse = false,

        .esc_neutral_us = ESC_NEUTRAL_DEFAULT,
        .esc_neutral_band_us = ESC_NEUTRAL_BAND_DEFAULT,
        .esc_forward_min_us = ESC_FORWARD_MIN_DEFAULT,
        .esc_forward_max_us = ESC_FORWARD_MAX_DEFAULT,
        .esc_reverse_min_us = ESC_REVERSE_MIN_DEFAULT,
        .esc_reverse_max_us = ESC_REVERSE_MAX_DEFAULT,

        .failsafe_timeout_ms = FAILSAFE_TIMEOUT_MS_DEFAULT,
        .reverse_neutral_dwell_ms = REVERSE_NEUTRAL_DWELL_MS_DEFAULT,

        .ch4_mode_switch_enabled = CH4_MODE_SWITCH_ENABLED_DEFAULT,
        .ch4_switch_threshold_us = CH4_SWITCH_THRESHOLD_US_DEFAULT,

        .deploy_servo_us = DEPLOY_SERVO_US_DEFAULT,
        .click_window_ms = CLICK_WINDOW_MS_DEFAULT,

        .spot_lock_deadband_m = SPOT_LOCK_DEADBAND_M_DEFAULT,
        .spot_lock_max_throttle_pct = SPOT_LOCK_MAX_THROTTLE_PCT_DEFAULT,
        .spot_lock_throttle_gain = SPOT_LOCK_THROTTLE_GAIN_DEFAULT,
        .spot_lock_servo_gain = SPOT_LOCK_SERVO_GAIN_DEFAULT,

        .goto_comms_timeout_ms = GOTO_COMMS_TIMEOUT_MS_DEFAULT,
    };

    *out = defaults;
}
