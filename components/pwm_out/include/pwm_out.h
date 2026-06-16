#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "safety_clamp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Conservative hard-clamp window for RC actuator pulses (microseconds).
 * SI-3: every output is forced into this inclusive range. These bounds bracket
 * the standard 1000..2000 us RC band with a small margin and are the last line
 * of defence regardless of upstream logic. */
#define PWM_OUT_MIN_US 900U
#define PWM_OUT_MAX_US 2100U

/* Neutral / center pulse width applied at boot (servo center, ESC neutral). */
#define PWM_OUT_NEUTRAL_US 1500U

/**
 * Logical output channels driven by the LEDC peripheral.
 *
 * PWM_OUT_SERVO -> GPIO18 (steering servo)
 * PWM_OUT_ESC   -> GPIO19 (bidirectional brushed ESC)
 */
typedef enum {
    PWM_OUT_SERVO = 0,
    PWM_OUT_ESC = 1,
    PWM_OUT_CHANNEL_COUNT
} PwmOutChannel;

/**
 * Initialise the LEDC timer and both output channels at 50 Hz / 16-bit.
 *
 * Fail-fast: returns the first underlying esp_err_t on failure. On success
 * both channels are configured but no duty is written yet; the caller must
 * call pwm_out_write_us to emit the safe neutral pulse.
 *
 * @return ESP_OK on success, otherwise the failing esp_err_t.
 */
esp_err_t pwm_out_init(void);

/**
 * Write a pulse width (microseconds) to a channel, applying the SI-3 hard
 * clamp internally before conversion to duty. No code path reaches the LEDC
 * duty register without passing through clamp_pwm_us.
 *
 * @param channel   Target output channel.
 * @param value_us  Requested pulse width; clamped to [PWM_OUT_MIN_US, PWM_OUT_MAX_US].
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for an unknown channel,
 *         otherwise the failing esp_err_t from the LEDC driver.
 */
esp_err_t pwm_out_write_us(PwmOutChannel channel, uint32_t value_us);

#ifdef __cplusplus
}
#endif
