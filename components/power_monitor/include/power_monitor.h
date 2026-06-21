#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure voltage interpretation helper for the future ADC-based battery monitor.
 *
 * It deliberately does NOT own ADC pinout or ESP-IDF ADC setup. The hardware
 * layer will feed millivolt samples here after calibration/scaling. This module
 * only answers: how should 12 V pack voltage be filtered and when should a
 * warning be raised without treating a short throttle sag as an immediate fault.
 */

typedef struct {
    uint16_t warn_mv;          /* filtered voltage below this starts warning timer */
    uint16_t clear_mv;         /* warning clears only at/above this voltage */
    uint16_t warn_frames;      /* consecutive low frames required before warning */
    uint16_t rest_frames;      /* neutral/low-load frames before rest estimate updates */
    uint8_t filtered_shift;    /* EMA: 1/2^shift of the new sample, 0 = immediate */
    uint8_t rest_shift;        /* EMA for rest estimate after rest_frames */
} power_monitor_cfg;

typedef struct {
    bool initialized;
    uint16_t instant_mv;
    uint16_t filtered_mv;
    uint16_t rest_estimate_mv;
    uint16_t low_frames;
    uint16_t neutral_frames;
    bool warning_active;
} power_monitor_state;

typedef struct {
    uint16_t instant_mv;
    uint16_t filtered_mv;
    uint16_t rest_estimate_mv;
    bool warning_active;
} power_monitor_output;

power_monitor_cfg power_monitor_default_cfg(void);
void power_monitor_init(power_monitor_state *state);
power_monitor_output power_monitor_update(power_monitor_state *state,
                                          const power_monitor_cfg *cfg,
                                          uint16_t sample_mv,
                                          bool esc_near_neutral);

#ifdef __cplusplus
}
#endif
