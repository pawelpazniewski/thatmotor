#include "led_pattern.h"

/* A symmetric 50%-duty blink: on for the first half of the period. */
static bool blink_50(uint32_t t_ms, uint32_t period_ms)
{
    return (t_ms % period_ms) < (period_ms / 2U);
}

/* Two short blinks at the start of a window: on during slots 0 and 2,
 * off during slots 1 and 3 (blink, gap, blink, gap). */
static bool double_blink_active(uint32_t phase_ms)
{
    uint32_t slot = phase_ms / LED_PATTERN_DOUBLE_BLINK_SLOT_MS;
    return slot == 0U || slot == 2U;
}

/* DISARMED base: slow 0.5 Hz blink. When !calibrated, overlay a double-blink in
 * the first part of each slow cycle so an uncalibrated unit is distinguishable. */
static bool disarmed_on(bool calibrated, uint32_t t_ms)
{
    if (calibrated) {
        return blink_50(t_ms, LED_PATTERN_DISARMED_PERIOD_MS);
    }
    uint32_t phase = t_ms % LED_PATTERN_DISARMED_PERIOD_MS;
    uint32_t overlay_window = LED_PATTERN_DOUBLE_BLINK_SLOT_MS * 4U;
    if (phase < overlay_window) {
        return double_blink_active(phase);
    }
    return blink_50(t_ms, LED_PATTERN_DISARMED_PERIOD_MS);
}

/* ESC_CALIBRATION: a repeating double-blink, off for the rest of the period. */
static bool calibration_on(uint32_t t_ms)
{
    uint32_t phase = t_ms % LED_PATTERN_CALIBRATION_PERIOD_MS;
    uint32_t blink_window = LED_PATTERN_DOUBLE_BLINK_SLOT_MS * 4U;
    if (phase >= blink_window) {
        return false;
    }
    return double_blink_active(phase);
}

bool led_pattern_on(sm_state state, bool calibrated, uint32_t t_ms)
{
    switch (state) {
    case SM_STATE_ARMED:
        return true;
    case SM_STATE_FAILSAFE:
        return blink_50(t_ms, LED_PATTERN_FAILSAFE_PERIOD_MS);
    case SM_STATE_ESC_CALIBRATION:
        return calibration_on(t_ms);
    case SM_STATE_DISARMED:
    default:
        return disarmed_on(calibrated, t_ms);
    }
}
