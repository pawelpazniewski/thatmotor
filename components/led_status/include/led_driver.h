#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Thin GPIO HAL for the on-board status LED (GPIO2), Unit 11.
 *
 * Owns the single GPIO line; all blink logic lives in led_pattern (pure). This
 * layer only configures the pin and writes the level computed by led_pattern.
 */

/** On-board LED GPIO on the ESP32 DevKit. */
#define LED_DRIVER_GPIO 2

/**
 * Configure the LED GPIO as a push-pull output, initially off. Fail-fast on any
 * driver error so a broken status line surfaces instead of running silently.
 *
 * @return ESP_OK on success, otherwise the failing esp_err_t.
 */
esp_err_t led_driver_init(void);

/**
 * Set the LED level.
 *
 * @param on  true -> LED on, false -> LED off.
 */
void led_driver_set(bool on);

#ifdef __cplusplus
}
#endif
