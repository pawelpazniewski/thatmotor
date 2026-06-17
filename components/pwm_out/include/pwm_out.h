#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "safety_clamp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Per-channel hard-clamp windows for RC actuator pulses (microseconds).
 * SI-3: every output is forced into the inclusive range of its channel before
 * conversion to duty. These are the last line of defence regardless of upstream
 * logic; pwm_out_write_us selects the window from the target channel.
 *
 * Servo window is the full electrical range of a 270 deg servo (500..2500 us)
 * so the user can drive wide endpoints (e.g. ~833/2167 us for ~180 deg).
 * ESC window stays at the conservative 1000..2000 us actuator band the WP880
 * calibration lives in and must NOT be widened (safety). */
#define PWM_OUT_SERVO_MIN_US 500U
#define PWM_OUT_SERVO_MAX_US 2500U
#define PWM_OUT_ESC_MIN_US 1000U
#define PWM_OUT_ESC_MAX_US 2000U

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
 * @param channel   Target output channel (selects the clamp window).
 * @param value_us  Requested pulse width; clamped to the channel's window
 *                  (servo [500,2500], ESC [1000,2000]).
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for an unknown channel,
 *         otherwise the failing esp_err_t from the LEDC driver.
 */
esp_err_t pwm_out_write_us(PwmOutChannel channel, uint32_t value_us);

#ifdef __cplusplus
}
#endif
