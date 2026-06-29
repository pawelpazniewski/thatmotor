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
    int32_t after_deadband = shape_deadband(normalized,
                                            params->throttle_deadband_us,
                                            params->rc_min_us, params->rc_mid_us,
                                            params->rc_max_us);
    return apply_reverse(after_deadband, params->throttle_reverse);
}

/* Step 6: asymmetric power limit BEFORE the ramp. A forward (positive) command
 * is capped to fwd_pct of full scale; a reverse (negative) command to rev_pct.
 * In-band commands pass through unchanged. */
static int32_t apply_power_limit(int32_t command, uint16_t fwd_pct,
                                 uint16_t rev_pct)
{
    int32_t fwd_limit =
        SIGNAL_NORMALIZED_FULL_SCALE * (int32_t)fwd_pct / PERCENT_FULL;
    int32_t rev_limit =
        SIGNAL_NORMALIZED_FULL_SCALE * (int32_t)rev_pct / PERCENT_FULL;
    if (command > fwd_limit) {
        return fwd_limit;
    }
    if (command < -rev_limit) {
        return -rev_limit;
    }
    return command;
}

/* Step 7: per-state override acts on the TARGET. NEUTRAL forces 0 so the ramp
 * soft-stops; SPOT_LOCK substitutes the regulator's forward command (already
 * capped + forward-only); TRACK keeps the limited stick command. The spot-lock
 * command still rides the same ramp -> map -> hard clamp below. */
static int32_t resolve_target(int32_t limited, throttle_target_mode mode,
                              int32_t spot_lock_cmd)
{
    if (mode == THROTTLE_TARGET_NEUTRAL) {
        return 0;
    }
    if (mode == THROTTLE_TARGET_SPOT_LOCK) {
        return spot_lock_cmd;
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

/* Neutral reference for the signed normalized command (0 = no spin). */
#define COMMAND_NEUTRAL 0

/* Sign of a signed command relative to neutral: -1, 0 or +1. */
static int32_t command_sign(int32_t command)
{
    if (command > COMMAND_NEUTRAL) {
        return 1;
    }
    if (command < COMMAND_NEUTRAL) {
        return -1;
    }
    return 0;
}

/* A flip is requested when the target opposes the current spin (both non-zero,
 * opposite signs). Starting from neutral in any direction is NOT a flip: the
 * motor is already stopped, so no anti-plugging dwell is needed. */
static bool is_reversing(int32_t target, int32_t current)
{
    if (target == COMMAND_NEUTRAL || current == COMMAND_NEUTRAL) {
        return false;
    }
    return command_sign(target) != command_sign(current);
}

/* Magnitude (distance from neutral) of a signed command. */
static int32_t command_magnitude(int32_t command)
{
    return command < 0 ? -command : command;
}

/* Ramp toward `target` choosing the rate by MAGNITUDE relative to neutral, not
 * numeric sign: moving AWAY from neutral (spinning up, either direction) uses
 * the gentle accel rate; moving TOWARD neutral (slowing down) uses the quick
 * decel rate. This keeps forward and reverse symmetric in feel - a gentle
 * spin-up and a quick decel on both sides - instead of swapping the rates for
 * reverse the way a sign-based ramp would. */
static int32_t accel_aware_step(int32_t value, int32_t target, int32_t accel_rate,
                                int32_t decel_rate)
{
    int32_t rate = command_magnitude(target) >= command_magnitude(value)
                       ? accel_rate
                       : decel_rate;
    return ramp_step(value, target, rate, rate);
}

/* Advance the ramp + dwell state by one cycle toward `target`.
 *
 * Three exclusive phases, in priority order:
 *   1. Dwell active  -> hold neutral, count the dwell down (motor parked).
 *   2. Reversing     -> ramp down to neutral; when it lands, arm the dwell.
 *   3. Normal        -> ramp toward the target.
 * The output value never crosses neutral while a dwell is pending. */
static void advance_ramp(throttle_ramp_state *st, int32_t target,
                         uint16_t reverse_dwell_frames,
                         const settings_params *params)
{
    int32_t rate_up = (int32_t)params->esc_ramp_up_us_per_cycle;
    int32_t rate_down = (int32_t)params->esc_ramp_down_us_per_cycle;

    if (st->dwell_remaining > 0) {
        st->value = COMMAND_NEUTRAL;
        st->dwell_remaining--;
        return;
    }
    if (is_reversing(target, st->value)) {
        st->value = ramp_step(st->value, COMMAND_NEUTRAL, rate_down, rate_down);
        if (st->value == COMMAND_NEUTRAL) {
            st->dwell_remaining = reverse_dwell_frames;
        }
        return;
    }
    st->value = accel_aware_step(st->value, target, rate_up, rate_down);
}

bool throttle_is_neutral(uint32_t raw_ch2_us, const settings_params *params)
{
    int32_t normalized = normalize_us(raw_ch2_us, params->rc_min_us,
                                      params->rc_mid_us, params->rc_max_us);
    int32_t after_deadband = shape_deadband(normalized,
                                            params->throttle_deadband_us,
                                            params->rc_min_us, params->rc_mid_us,
                                            params->rc_max_us);
    return after_deadband == 0;
}

uint32_t throttle_chain_step(uint32_t raw_ch2_us, throttle_target_mode mode,
                             int32_t spot_lock_cmd,
                             const settings_params *params,
                             uint16_t reverse_dwell_frames,
                             throttle_ramp_state *st)
{
    int32_t command = shape_command(raw_ch2_us, params);
    int32_t limited = apply_power_limit(command, params->max_throttle_fwd_pct,
                                        params->max_throttle_rev_pct);
    int32_t target = resolve_target(limited, mode, spot_lock_cmd);

    advance_ramp(st, target, reverse_dwell_frames, params);

    uint32_t esc_us = map_to_esc_us(st->value, params);
    PwmWindow window = {.min_us = ESC_WINDOW_MIN_US, .max_us = ESC_WINDOW_MAX_US};
    return clamp_pwm_us(esc_us, window);
}
