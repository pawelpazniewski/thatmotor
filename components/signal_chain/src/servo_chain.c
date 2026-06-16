#include "signal_chain.h"

#include <stdint.h>

#include "chain_math.h"
#include "ramp.h"
#include "safety_clamp.h"

/* Output sanity window for the servo pin (SI-3); clamp is the last step. */
#define SERVO_WINDOW_MIN_US 1000U
#define SERVO_WINDOW_MAX_US 2000U

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
    int32_t deadband = deadband_us_to_normalized(params->steer_deadband_us,
                                                 params->rc_min_us,
                                                 params->rc_mid_us,
                                                 params->rc_max_us);
    int32_t after_deadband = apply_deadband(normalized, deadband);
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
    return map_normalized_to_us(command, params->servo_min_us,
                                servo_center_us(params), params->servo_max_us);
}

uint32_t servo_chain_step(uint32_t raw_ch1_us, servo_target_mode mode,
                          const settings_params *params, int32_t *slew_state)
{
    int32_t command = shape_command(raw_ch1_us, params);
    uint32_t target_us = resolve_target_us(command, mode, params);

    *slew_state = slew_step(*slew_state, (int32_t)target_us,
                            (int32_t)params->servo_slew_us_per_cycle);

    PwmWindow window = {.min_us = SERVO_WINDOW_MIN_US,
                        .max_us = SERVO_WINDOW_MAX_US};
    return clamp_pwm_us((uint32_t)*slew_state, window);
}
