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
 * A single pure function maps (control state, calibrated flag, time) to the
 * instantaneous on/off level of the on-board LED (GPIO2). No I/O, no globals,
 * no IDF dependencies: fully host-testable and deterministic in t_ms.
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

#ifdef __cplusplus
}
#endif
