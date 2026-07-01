#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "state_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Status LED blink patterns (Unit 11), pure logic.
 *
 * Pure functions map (control state, calibrated flag, time) to the
 * instantaneous on/off level and the RGB colour of the on-board WS2812 LED
 * (GPIO48). No I/O, no globals, no IDF dependencies: fully host-testable and
 * deterministic in t_ms.
 *
 * Patterns (rozwiazanie otwartego pytania R8):
 *  - DISARMED:        slow blink @ LED_PATTERN_DISARMED_HZ (0.5 Hz, 50% duty).
 *                     If !calibrated (UNCALIBRATED / defaults), a short
 *                     double-blink overlay is superimposed once per slow cycle
 *                     so the operator can distinguish an uncalibrated unit.
 *  - ARMED:           solid on.
 *  - FAILSAFE:        fast blink @ LED_PATTERN_FAILSAFE_HZ (5 Hz) the whole
 *                     time, independent of the previous state (R6/R8).
 *  - ESC_CALIBRATION: characteristic double-blink, independent of calibrated.
 *
 * The "calibrated" overlay only applies in DISARMED; ARMED/FAILSAFE/CALIBRATION
 * ignore it (their patterns are unambiguous on their own).
 */

/* Slow DISARMED blink: 0.5 Hz -> 2000 ms period, 50% duty. */
#define LED_PATTERN_DISARMED_PERIOD_MS 2000U

/* Fast FAILSAFE blink: 5 Hz -> 200 ms period, 50% duty. */
#define LED_PATTERN_FAILSAFE_PERIOD_MS 200U

/* Double-blink unit period (one short blink slot). Two blinks fit in the first
 * 4 slots; the rest of the cycle is the base pattern (DISARMED) or off. */
#define LED_PATTERN_DOUBLE_BLINK_SLOT_MS 150U

/* The repeat period of the ESC_CALIBRATION double-blink pattern. */
#define LED_PATTERN_CALIBRATION_PERIOD_MS 1200U

/* DEPLOY: a characteristic TRIPLE-blink burst repeated every 2 s, off for the
 * rest of the period. Distinct from the DISARMED double-blink overlay (only when
 * uncalibrated) and the ESC_CALIBRATION double-blink (1200 ms period): three
 * blinks in a 2 s window unambiguously reads "motor raised". */
#define LED_PATTERN_DEPLOY_PERIOD_MS 2000U

/* On-board WS2812 brightness as a percentage of full output. Keeps the
 * indicator comfortable (full white is blinding) and saves power. */
#define LED_PATTERN_BRIGHTNESS_PERCENT 25U

/* A 24-bit RGB colour for the on-board WS2812 status LED. Channels are the
 * final values to emit (brightness already applied), 0..255 each. */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} LedColor;

/**
 * Compute the instantaneous LED level for the current state at time t_ms (pure).
 *
 * @param state       Current control state.
 * @param calibrated  Whether the active params are a real stored calibration
 *                    (only affects the DISARMED overlay).
 * @param t_ms        Monotonic milliseconds (phase reference for the blink).
 * @return true when the LED should be on this instant, false when off.
 */
bool led_pattern_on(sm_state state, bool calibrated, uint32_t t_ms);

/**
 * Compute the instantaneous RGB colour for the current state at t_ms (pure).
 *
 * Combines the per-state blink phase (led_pattern_on) with a per-state colour:
 * the LED shows the state colour during the on-phase and {0,0,0} during the
 * off-phase, so the existing blink semantics carry over unchanged and colour
 * adds a second, redundant channel of meaning. Brightness is scaled to
 * LED_PATTERN_BRIGHTNESS_PERCENT. No I/O: host-testable and deterministic.
 *
 * Colours: DISARMED amber · ARMED green · FAILSAFE red ·
 * ESC_CALIBRATION blue · DEPLOY cyan.
 *
 * @param state       Current control state.
 * @param calibrated  Whether the active params are a real stored calibration
 *                    (only affects the DISARMED blink overlay, not the colour).
 * @param t_ms        Monotonic milliseconds (phase reference for the blink).
 * @return The RGB colour to emit this instant ({0,0,0} on the off-phase).
 */
LedColor led_pattern_color(sm_state state, bool calibrated, uint32_t t_ms);

#ifdef __cplusplus
}
#endif
