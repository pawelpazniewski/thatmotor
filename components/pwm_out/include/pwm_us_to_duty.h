#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LEDC timer resolution used for the servo/ESC PWM channels. Capped at the
 * ESP32-S3 hardware ceiling (14-bit; the classic ESP32 allowed up to 20-bit).
 * At 50 Hz this still gives ~1.22 us/step — finer than any RC actuator. */
#define PWM_DUTY_RESOLUTION_BITS 14U

/* PWM carrier frequency for RC actuators (servo + ESC), in Hz. */
#define PWM_FREQ_HZ 50U

/* Full PWM period in microseconds, derived from PWM_FREQ_HZ (1 / 50 Hz). */
#define PWM_PERIOD_US (1000000U / PWM_FREQ_HZ)

/* Number of distinct duty steps for PWM_DUTY_RESOLUTION_BITS (2^14). */
#define PWM_DUTY_MAX (1U << PWM_DUTY_RESOLUTION_BITS)

/**
 * Convert a pulse width in microseconds to a 14-bit LEDC duty value.
 *
 * Pure function: duty = round(value_us * PWM_DUTY_MAX / PWM_PERIOD_US).
 * At 50 Hz / 14-bit this yields 1000/1500/2000 us -> 819/1229/1638.
 *
 * The caller is responsible for clamping value_us to a safe window first;
 * this function performs no range limiting of its own.
 *
 * @param value_us  Pulse width in microseconds.
 * @return          Duty value in [0, PWM_DUTY_MAX].
 */
uint32_t pwm_us_to_duty(uint32_t value_us);

#ifdef __cplusplus
}
#endif
