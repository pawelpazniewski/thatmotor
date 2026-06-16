#pragma once

/* Internal per-field sanity ranges (inclusive) and conservative default values
 * shared between settings_defaults.c and settings_validate.c. Kept in one place
 * so the defaults provably satisfy the validator. All microseconds unless the
 * name says otherwise. Sources of magic-number choices documented inline. */

/* RC input calibration: standard 1000/1500/2000 us band, generous bounds. */
#define RC_US_MIN 800U
#define RC_US_MAX 2200U
#define RC_MIN_DEFAULT 1000U
#define RC_MID_DEFAULT 1500U
#define RC_MAX_DEFAULT 2000U

/* Servo endpoints and slew. Servo endpoints live in the actuator pulse band. */
#define SERVO_US_MIN 1000U
#define SERVO_US_MAX 2000U
#define SERVO_MIN_DEFAULT 1100U
#define SERVO_MAX_DEFAULT 1900U
/* Slew default is gentle (smooth steering); 1..200 us/cycle is a wide window. */
#define SERVO_SLEW_MIN 1U
#define SERVO_SLEW_MAX 200U
#define SERVO_SLEW_DEFAULT 10U

/* Deadbands. Steering deadband defaults to 0 (plan: steer deadband = 0). */
#define DEADBAND_MAX 300U
#define STEER_DEADBAND_DEFAULT 0U
/* Throttle deadband wide by default so small stick noise never spins motor. */
#define THROTTLE_DEADBAND_DEFAULT 80U

/* Throttle ramps gentle by default (slow accel/decel = soft start/stop). */
#define ESC_RAMP_MIN 1U
#define ESC_RAMP_MAX 200U
#define ESC_RAMP_UP_DEFAULT 5U
#define ESC_RAMP_DOWN_DEFAULT 10U

/* Power limit: low max throttle by default (conservative; R16). */
#define MAX_THROTTLE_PCT_MIN 1U
#define MAX_THROTTLE_PCT_MAX 100U
#define MAX_THROTTLE_PCT_DEFAULT 30U

/* ESC output calibration band (WP880 maps inside the actuator pulse range). */
#define ESC_US_MIN 1000U
#define ESC_US_MAX 2000U
#define ESC_NEUTRAL_MIN 1400U /* plan: escNeutralUs 1400..1600 */
#define ESC_NEUTRAL_MAX 1600U
#define ESC_NEUTRAL_DEFAULT 1500U
#define ESC_NEUTRAL_BAND_MIN 0U
#define ESC_NEUTRAL_BAND_MAX 200U
#define ESC_NEUTRAL_BAND_DEFAULT 40U
#define ESC_FORWARD_MIN_DEFAULT 1550U
#define ESC_FORWARD_MAX_DEFAULT 1900U
#define ESC_REVERSE_MIN_DEFAULT 1450U
#define ESC_REVERSE_MAX_DEFAULT 1100U

/* Failsafe timeout: time without a valid pulse before treating RC as lost.
 * Generous default; tuned after the receiver frame-rate measurement. */
#define FAILSAFE_TIMEOUT_MS_MIN 20U
#define FAILSAFE_TIMEOUT_MS_MAX 2000U
#define FAILSAFE_TIMEOUT_MS_DEFAULT 200U

/* Dwell at neutral on a fast forward<->reverse flip. 0 = disabled until the
 * WP880 plugging behaviour is measured (kontekst: open item). */
#define REVERSE_NEUTRAL_DWELL_MS_MIN 0U
#define REVERSE_NEUTRAL_DWELL_MS_MAX 2000U
#define REVERSE_NEUTRAL_DWELL_MS_DEFAULT 0U
