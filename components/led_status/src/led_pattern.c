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

/* Three short blinks at the start of a window: on during slots 0, 2 and 4
 * (blink, gap, blink, gap, blink), off otherwise. */
static bool triple_blink_active(uint32_t phase_ms)
{
    uint32_t slot = phase_ms / LED_PATTERN_DOUBLE_BLINK_SLOT_MS;
    return slot == 0U || slot == 2U || slot == 4U;
}

/* DEPLOY: a repeating TRIPLE-blink burst, off for the rest of the 2 s period. */
static bool deploy_on(uint32_t t_ms)
{
    uint32_t phase = t_ms % LED_PATTERN_DEPLOY_PERIOD_MS;
    uint32_t blink_window = LED_PATTERN_DOUBLE_BLINK_SLOT_MS * 6U;
    if (phase >= blink_window) {
        return false;
    }
    return triple_blink_active(phase);
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
    case SM_STATE_DEPLOY:
        return deploy_on(t_ms);
    case SM_STATE_DISARMED:
    default:
        return disarmed_on(calibrated, t_ms);
    }
}

/* Scale one 0..255 channel down to LED_PATTERN_BRIGHTNESS_PERCENT of full. */
static uint8_t scale_brightness(uint8_t channel)
{
    return (uint8_t)((uint16_t)channel * LED_PATTERN_BRIGHTNESS_PERCENT / 100U);
}

/* Full-brightness colour per state; brightness is applied by led_pattern_color.
 * Mirrors the led_pattern_on switch so colour and blink phase stay in sync. */
static LedColor base_color_for_state(sm_state state)
{
    switch (state) {
    case SM_STATE_ARMED:
        return (LedColor){.r = 0U, .g = 255U, .b = 0U}; /* green */
    case SM_STATE_FAILSAFE:
        return (LedColor){.r = 255U, .g = 0U, .b = 0U}; /* red */
    case SM_STATE_ESC_CALIBRATION:
        return (LedColor){.r = 0U, .g = 0U, .b = 255U}; /* blue */
    case SM_STATE_DEPLOY:
        return (LedColor){.r = 0U, .g = 255U, .b = 255U}; /* cyan */
    case SM_STATE_DISARMED:
    default:
        return (LedColor){.r = 255U, .g = 160U, .b = 0U}; /* amber */
    }
}

LedColor led_pattern_color(sm_state state, bool calibrated, uint32_t t_ms)
{
    if (!led_pattern_on(state, calibrated, t_ms)) {
        return (LedColor){.r = 0U, .g = 0U, .b = 0U};
    }
    LedColor base = base_color_for_state(state);
    return (LedColor){
        .r = scale_brightness(base.r),
        .g = scale_brightness(base.g),
        .b = scale_brightness(base.b),
    };
}
