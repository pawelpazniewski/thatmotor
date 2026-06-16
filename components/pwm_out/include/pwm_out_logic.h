#pragma once

#include <stdint.h>

#include "safety_clamp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Number of logical output channels (must match PwmOutChannel in pwm_out.h).
 * Kept here as a framework-agnostic constant so the pure logic and its host
 * tests do not depend on the LEDC-linking pwm_out.h. */
#define PWM_OUT_LOGIC_CHANNEL_COUNT 2

/* Result of resolving an output write before it touches the LEDC peripheral. */
typedef enum {
    PWM_OUT_LOGIC_OK = 0,
    PWM_OUT_LOGIC_INVALID_CHANNEL = 1,
} PwmOutLogicResult;

/**
 * Pure resolution of a channel write: validate the channel, then apply the
 * SI-3 hard clamp before converting the pulse width to a duty value.
 *
 * This is the framework-agnostic core of pwm_out_write_us. It performs no I/O
 * and links no peripheral, so it is host-testable directly. The production
 * pwm_out_write_us calls it and only forwards the resulting duty to LEDC,
 * guaranteeing there is no code path to the duty register that bypasses the
 * clamp.
 *
 * On an invalid channel it returns PWM_OUT_LOGIC_INVALID_CHANNEL and leaves
 * *out_duty untouched.
 *
 * @param channel   Channel index in [0, PWM_OUT_LOGIC_CHANNEL_COUNT).
 * @param value_us  Requested pulse width in microseconds (unclamped).
 * @param window    Safety window applied via clamp_pwm_us.
 * @param out_duty  Output: clamped pulse width converted to duty (on OK only).
 * @return          PWM_OUT_LOGIC_OK on success, else PWM_OUT_LOGIC_INVALID_CHANNEL.
 */
PwmOutLogicResult pwm_out_resolve_duty(int channel, uint32_t value_us,
                                       PwmWindow window, uint32_t *out_duty);

#ifdef __cplusplus
}
#endif
