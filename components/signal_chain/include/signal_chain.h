#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Signal chains (pure logic) for throttle (CH2 -> ESC) and servo (CH1 -> servo).
 *
 * Both chains follow the fixed processing order from the requirements doc and
 * are framework-agnostic: input is a raw RC pulse width plus the active params,
 * the previous ramp/slew state and a per-state target override; output is a
 * pulse width already routed through the SI-3 hard clamp. No I/O, no globals.
 *
 * Fixed, enforced order (requirements doc "Lancuchy przetwarzania sygnalu"):
 *   normalize -> deadband -> reverse -> (power limit | endpoints) -> TARGET
 *   -> ramp/slew -> map to actuator us -> HARD CLAMP.
 * Deadband acts on the normalized target BEFORE scaling/ramp; reverse acts
 * AFTER deadband (so neutral stays neutral); the limit/endpoints act BEFORE the
 * ramp; the per-state override acts on the TARGET (not the raw output) so the
 * ramp/slew realises a smooth transition to neutral/center.
 */

/** Full-scale magnitude of the normalized signed command (+/- this value). */
#define SIGNAL_NORMALIZED_FULL_SCALE 1000

/**
 * Per-state override applied to the throttle target (step 7 of the chain).
 *
 * DISARMED and FAILSAFE force the target to 0 (neutral); the ramp then performs
 * the soft-stop. ARMED tracks the processed stick command. ESC calibration is a
 * separate code path (Unit 9) and is not modelled here.
 */
typedef enum {
    THROTTLE_TARGET_NEUTRAL = 0, /* DISARMED / FAILSAFE -> target 0 */
    THROTTLE_TARGET_TRACK = 1,   /* ARMED -> track processed stick */
} throttle_target_mode;

/**
 * Per-state override applied to the servo target (step 7 of the chain).
 *
 * RC valid tracks the processed steering command; FAILSAFE forces the target to
 * center; the slew limiter then drives the servo there.
 */
typedef enum {
    SERVO_TARGET_TRACK = 0,  /* RC valid -> track processed stick */
    SERVO_TARGET_CENTER = 1, /* FAILSAFE -> target center */
} servo_target_mode;

/**
 * Carry-over state for the throttle ramp with anti-plugging direction manager.
 *
 * `value` is the ramped command in normalized units (signed, 0 = neutral).
 * `dwell_remaining` is the number of cycles the output is held at neutral after
 * a forward<->reverse flip reaches neutral, counted down to 0. While it is
 * non-zero the output cannot cross to the other side of neutral (anti-plugging
 * invariant: the prop fully stops before it spins up the other way).
 */
typedef struct {
    int32_t value;           /* ramped command, normalized signed units */
    uint16_t dwell_remaining; /* cycles still held at neutral after a flip */
} throttle_ramp_state;

/**
 * Run one throttle control cycle with the direction-change manager.
 *
 * Steps 3-10 of the throttle chain plus the reverse-direction manager. When the
 * requested direction opposes the current spin, the output first ramps down to
 * neutral, is then held at neutral for `reverse_dwell_frames` cycles, and only
 * afterwards ramps up in the new direction. The output NEVER crosses to the
 * other side of neutral until the full dwell has elapsed. The result is the ESC
 * pulse width AFTER the SI-3 hard clamp; no chain output bypasses the clamp.
 *
 * @param raw_ch2_us           Raw CH2 pulse width in microseconds.
 * @param mode                 Per-state target override.
 * @param params               Active control parameters (must be non-NULL).
 * @param reverse_dwell_frames Neutral dwell expressed in cycles (computed by the
 *                             caller so this function stays period-agnostic).
 * @param st                   Ramp + dwell carry-over state, updated in place
 *                             (must be non-NULL).
 * @return ESC pulse width in microseconds, clamped to the actuator window.
 */
uint32_t throttle_chain_step(uint32_t raw_ch2_us, throttle_target_mode mode,
                             const settings_params *params,
                             uint16_t reverse_dwell_frames,
                             throttle_ramp_state *st);

/**
 * Run one servo control cycle.
 *
 * Steps 3-10 of the servo chain. The result is the servo pulse width AFTER the
 * SI-3 hard clamp; no chain output bypasses the clamp.
 *
 * @param raw_ch1_us  Raw CH1 pulse width in microseconds.
 * @param mode        Per-state target override.
 * @param params      Active control parameters (must be non-NULL).
 * @param slew_state  Previous slewed servo pulse width in microseconds; updated
 *                    in place to the new slewed pulse width (must be non-NULL).
 * @return Servo pulse width in microseconds, clamped to the actuator window.
 */
uint32_t servo_chain_step(uint32_t raw_ch1_us, servo_target_mode mode,
                          const settings_params *params, int32_t *slew_state);

/**
 * Whether the throttle stick is within its neutral band this cycle.
 *
 * Applies the SAME normalize + throttle-deadband steps the throttle chain uses,
 * so the arming "throttle neutral" gate (R7) and the chain agree on what neutral
 * means. Independent of arming state and of the reverse flag (reverse acts after
 * the deadband and never moves a neutral command).
 *
 * @param raw_ch2_us  Raw CH2 pulse width in microseconds.
 * @param params      Active control parameters (must be non-NULL).
 * @return true when the post-deadband throttle command is exactly neutral.
 */
bool throttle_is_neutral(uint32_t raw_ch2_us, const settings_params *params);

#ifdef __cplusplus
}
#endif
