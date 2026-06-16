#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LEDC timer resolution used for the servo/ESC PWM channels. */
#define PWM_DUTY_RESOLUTION_BITS 16U

/* PWM carrier frequency for RC actuators (servo + ESC), in Hz. */
#define PWM_FREQ_HZ 50U

/* Full PWM period in microseconds, derived from PWM_FREQ_HZ (1 / 50 Hz). */
#define PWM_PERIOD_US (1000000U / PWM_FREQ_HZ)

/* Number of distinct duty steps for PWM_DUTY_RESOLUTION_BITS (2^16). */
#define PWM_DUTY_MAX (1U << PWM_DUTY_RESOLUTION_BITS)

/**
 * Convert a pulse width in microseconds to a 16-bit LEDC duty value.
 *
 * Pure function: duty = round(value_us * PWM_DUTY_MAX / PWM_PERIOD_US).
 * At 50 Hz / 16-bit this yields 1000/1500/2000 us -> 3277/4915/6554.
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
