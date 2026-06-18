#include "signal_chain.h"

#include <stdint.h>

#include "chain_math.h"
#include "ramp.h"
#include "safety_clamp.h"

/* Output sanity window for the servo pin (SI-3); clamp is the last step. Spans
 * the full electrical range of a 270 deg servo (500..2500 us) so configured
 * endpoints (e.g. ~833/2167 us for ~180 deg) pass through unclamped; 500/2500
 * is the hard electrical limit. */
#define SERVO_WINDOW_MIN_US 500U
#define SERVO_WINDOW_MAX_US 2500U

/* Servo center is the midpoint of the configured endpoints. */
static uint32_t servo_center_us(const settings_params *params)
{
    return ((uint32_t)params->servo_min_us + (uint32_t)params->servo_max_us) / 2U;
}

/* Steps 3-5: normalize -> steering deadband -> reverse (reverse after deadband
 * so a centered command stays centered). */
static int32_t shape_command(uint32_t raw_us, const settings_params *params)
{
    int32_t normalized = normalize_us(raw_us, params->rc_min_us,
                                      params->rc_mid_us, params->rc_max_us);
    int32_t after_deadband = shape_deadband(normalized,
                                            params->steer_deadband_us,
                                            params->rc_min_us, params->rc_mid_us,
                                            params->rc_max_us);
    return apply_reverse(after_deadband, params->servo_reverse);
}

/* Steps 6-7: map command onto the servo endpoints, then apply the per-state
 * override on the TARGET (FAILSAFE -> center so the slew drives there). */
static uint32_t resolve_target_us(int32_t command, servo_target_mode mode,
                                  const settings_params *params)
{
    if (mode == SERVO_TARGET_CENTER) {
        return servo_center_us(params);
    }
    if (mode == SERVO_TARGET_DEPLOY) {
        return params->deploy_servo_us; /* slew here; the hard clamp follows */
    }
    return map_normalized_to_us(command, params->servo_min_us,
                                servo_center_us(params), params->servo_max_us);
}

/* Add the signed neutral trim to the slewed output, before the hard clamp, so
 * neutral/track/center/deploy all shift uniformly. A negative sum is floored at
 * 0 (the clamp would cut it to the window minimum anyway) to avoid undefined
 * behaviour when casting a negative int32 to uint32. */
static uint32_t apply_trim(int32_t slewed_us, int16_t trim_us)
{
    int32_t trimmed = slewed_us + (int32_t)trim_us;
    if (trimmed < 0) {
        trimmed = 0;
    }
    return (uint32_t)trimmed;
}

uint32_t servo_chain_step(uint32_t raw_ch1_us, servo_target_mode mode,
                          const settings_params *params, int32_t *slew_state)
{
    int32_t command = shape_command(raw_ch1_us, params);
    uint32_t target_us = resolve_target_us(command, mode, params);

    *slew_state = slew_step(*slew_state, (int32_t)target_us,
                            (int32_t)params->servo_slew_us_per_cycle);

    uint32_t trimmed_us = apply_trim(*slew_state, params->servo_trim_us);
    PwmWindow window = {.min_us = SERVO_WINDOW_MIN_US,
                        .max_us = SERVO_WINDOW_MAX_US};
    return clamp_pwm_us(trimmed_us, window);
}

int16_t servo_trim_stepped(int16_t current, int dir, int16_t step,
                           int16_t max_abs)
{
    int32_t next = (int32_t)current;
    if (dir > 0) {
        next += step;
    } else if (dir < 0) {
        next -= step;
    }
    if (next > max_abs) {
        next = max_abs;
    } else if (next < -max_abs) {
        next = -max_abs;
    }
    return (int16_t)next;
}
