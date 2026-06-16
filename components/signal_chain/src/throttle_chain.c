#include "signal_chain.h"

#include <stdint.h>

#include "chain_math.h"
#include "ramp.h"
#include "safety_clamp.h"

/* Output sanity window for the ESC pin (SI-3). Matches the actuator pulse band
 * the ESC calibration lives in; the clamp is the last, unconditional step. */
#define ESC_WINDOW_MIN_US 1000U
#define ESC_WINDOW_MAX_US 2000U

#define PERCENT_FULL 100

/* Steps 3-5: normalize -> throttle deadband -> reverse (reverse after deadband
 * so a neutral command stays neutral). */
static int32_t shape_command(uint32_t raw_us, const settings_params *params)
{
    int32_t normalized = normalize_us(raw_us, params->rc_min_us,
                                      params->rc_mid_us, params->rc_max_us);
    int32_t deadband = deadband_us_to_normalized(params->throttle_deadband_us,
                                                 params->rc_min_us,
                                                 params->rc_mid_us,
                                                 params->rc_max_us);
    int32_t after_deadband = apply_deadband(normalized, deadband);
    return apply_reverse(after_deadband, params->throttle_reverse);
}

/* Step 6: power limit (forward/reverse) BEFORE the ramp. Scales the magnitude
 * to at most max_throttle_pct of full scale. */
static int32_t apply_power_limit(int32_t command, uint16_t max_throttle_pct)
{
    int32_t limit = SIGNAL_NORMALIZED_FULL_SCALE * (int32_t)max_throttle_pct /
                    PERCENT_FULL;
    if (command > limit) {
        return limit;
    }
    if (command < -limit) {
        return -limit;
    }
    return command;
}

/* Step 7: per-state override acts on the TARGET. NEUTRAL forces 0 so the ramp
 * soft-stops; TRACK keeps the limited stick command. */
static int32_t resolve_target(int32_t limited, throttle_target_mode mode)
{
    if (mode == THROTTLE_TARGET_NEUTRAL) {
        return 0;
    }
    return limited;
}

/* Step 9: map ramped command onto the ESC calibration window. */
static uint32_t map_to_esc_us(int32_t command, const settings_params *params)
{
    return map_normalized_to_us(command, params->esc_reverse_max_us,
                                params->esc_neutral_us,
                                params->esc_forward_max_us);
}

uint32_t throttle_chain_step(uint32_t raw_ch2_us, throttle_target_mode mode,
                             const settings_params *params, int32_t *ramp_state)
{
    int32_t command = shape_command(raw_ch2_us, params);
    int32_t limited = apply_power_limit(command, params->max_throttle_pct);
    int32_t target = resolve_target(limited, mode);

    *ramp_state = ramp_step(*ramp_state, target,
                            (int32_t)params->esc_ramp_up_us_per_cycle,
                            (int32_t)params->esc_ramp_down_us_per_cycle);

    uint32_t esc_us = map_to_esc_us(*ramp_state, params);
    PwmWindow window = {.min_us = ESC_WINDOW_MIN_US, .max_us = ESC_WINDOW_MAX_US};
    return clamp_pwm_us(esc_us, window);
}
