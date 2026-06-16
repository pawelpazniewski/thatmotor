#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Shared pure building blocks for the throttle and servo chains. Kept in one
 * place so both chains apply normalization, deadband and reverse identically
 * (no duplicated logic, single definition of the fixed order).
 */

/**
 * Normalize a raw pulse width to a signed command in
 * [-SIGNAL_NORMALIZED_FULL_SCALE, +SIGNAL_NORMALIZED_FULL_SCALE] using the RC
 * input calibration (min/mid/max). Values below min map to -full, above max to
 * +full; mid maps to 0. The two half-ranges are scaled independently so an
 * off-center mid still yields a symmetric command.
 *
 * @param raw_us  Raw pulse width in microseconds.
 * @param min_us  Stick full one way.
 * @param mid_us  Stick centre.
 * @param max_us  Stick full other way.
 * @return Signed normalized command, saturated to +/- full scale.
 */
int32_t normalize_us(uint32_t raw_us, uint16_t min_us, uint16_t mid_us,
                     uint16_t max_us);

/**
 * Apply a symmetric deadband around zero on a normalized command: magnitudes at
 * or below `deadband` collapse to 0, larger magnitudes pass through unchanged.
 * The deadband is expressed in normalized units (same scale as the command).
 *
 * @param value     Normalized command.
 * @param deadband  Deadband half-width in normalized units (>= 0).
 * @return 0 inside the band, otherwise the value unchanged.
 */
int32_t apply_deadband(int32_t value, int32_t deadband);

/**
 * Negate the command when reverse is enabled. Applied AFTER the deadband so a
 * neutral (0) command stays neutral regardless of the reverse flag.
 *
 * @param value    Normalized command (post-deadband).
 * @param reverse  True to invert direction.
 * @return Possibly negated command.
 */
int32_t apply_reverse(int32_t value, bool reverse);

/**
 * Apply a deadband expressed in input microseconds to a normalized command,
 * converting the threshold using the SAME calibration half-range (mid..min or
 * mid..max) that normalize_us scaled the command's side with. This keeps the
 * dead zone a consistent input deflection on both sides of an off-center mid:
 * the same microsecond deadband around centre maps to neutral whether the stick
 * is pushed toward min or toward max, rather than borrowing the nearer half for
 * both sides.
 *
 * @param value        Normalized command (post-normalize, pre-reverse).
 * @param deadband_us  Deadband half-width in input microseconds.
 * @param min_us       Stick full one way.
 * @param mid_us       Stick centre.
 * @param max_us       Stick full other way.
 * @return 0 inside the per-side band, otherwise the value unchanged.
 */
int32_t shape_deadband(int32_t value, uint16_t deadband_us, uint16_t min_us,
                       uint16_t mid_us, uint16_t max_us);

/**
 * Linearly map a normalized command in [-full, +full] onto an output pulse
 * window [min_us, max_us] with `center_us` at command 0. The two half-ranges
 * are scaled independently so an asymmetric window keeps 0 -> center.
 *
 * @param value     Normalized command.
 * @param min_us    Output pulse for -full.
 * @param center_us Output pulse for 0.
 * @param max_us    Output pulse for +full.
 * @return Mapped pulse width in microseconds.
 */
uint32_t map_normalized_to_us(int32_t value, uint32_t min_us, uint32_t center_us,
                              uint32_t max_us);

#ifdef __cplusplus
}
#endif
