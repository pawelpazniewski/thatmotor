#pragma once

#include "esp_err.h"
#include "led_pattern.h" /* LedColor */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Thin WS2812 HAL for the on-board RGB status LED (GPIO48 on the ESP32-S3
 * DevKit), Unit 11. Owns the single addressable LED via the RMT TX peripheral;
 * all colour/blink logic lives in led_pattern (pure). This layer only drives
 * the WS2812 data line with the colour computed by led_pattern.
 */

/** On-board WS2812 RGB data GPIO on the ESP32-S3 DevKit. */
#define LED_DRIVER_GPIO 48

/**
 * Configure the RMT TX channel + WS2812 byte encoder and latch the LED off.
 * Fail-fast on any driver error so a broken status line surfaces instead of
 * running silently.
 *
 * @return ESP_OK on success, otherwise the failing esp_err_t.
 */
esp_err_t led_driver_init(void);

/**
 * Drive the WS2812 with one RGB colour. Best-effort and non-blocking-safe to
 * call every control-loop tick; a transmit error is dropped (the LED is a
 * status indicator, not a safety output).
 *
 * @param color  Final per-channel RGB to emit (brightness already applied).
 */
void led_driver_show(LedColor color);

#ifdef __cplusplus
}
#endif
