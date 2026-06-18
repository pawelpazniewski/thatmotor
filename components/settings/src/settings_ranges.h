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

/* Servo endpoints and slew. Endpoints span the full 270 deg servo electrical
 * range (500..2500 us) so the user can set wide endpoints (e.g. ~833/2167 us
 * for ~180 deg). Cross-field servo_min < servo_max is still enforced. */
#define SERVO_US_MIN 500U
#define SERVO_US_MAX 2500U
#define SERVO_MIN_DEFAULT 833U
#define SERVO_MAX_DEFAULT 2167U
/* Slew default is gentle (smooth steering); 1..200 us/cycle is a wide window. */
#define SERVO_SLEW_MIN 1U
#define SERVO_SLEW_MAX 200U
#define SERVO_SLEW_DEFAULT 10U

/* Servo neutral trim (SIGNED): range/step/default constants live in the public
 * settings_model.h (the shared data contract), because the control loop also
 * needs the step/max to drive the panel's live Step Left/Right. They are
 * included here via settings_model.h, so the validator and defaults use the same
 * single definition. */

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

/* Power limit: asymmetric forward/reverse caps (percent of full scale). 0..100
 * for both; 0 simply disables that direction. Forward default higher than
 * reverse (a kayak motor needs more forward authority than reverse). */
#define MAX_THROTTLE_PCT_MIN 0U
#define MAX_THROTTLE_PCT_MAX 100U
#define MAX_THROTTLE_FWD_PCT_DEFAULT 90U
#define MAX_THROTTLE_REV_PCT_DEFAULT 50U

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

/* Dwell at neutral on a fast forward<->reverse flip (anti-plugging): the prop is
 * held stopped at neutral this long before it spins up the other way. Default
 * 400 ms keeps the dwell active out of the box; 0 disables it. */
#define REVERSE_NEUTRAL_DWELL_MS_MIN 0U
#define REVERSE_NEUTRAL_DWELL_MS_MAX 2000U
#define REVERSE_NEUTRAL_DWELL_MS_DEFAULT 400U

/* CH4 mode-switch position switch. The threshold lives in the RC pulse band;
 * default 1500 us is the midpoint of a typical 2-position switch (~1000/~2000
 * us), so above it reads "high" (arm) and below it reads "low" (disarm). The
 * debounce-frame count is a property of the switch interpreter and is defined
 * in ch4_switch.h. */
#define CH4_MODE_SWITCH_ENABLED_DEFAULT true
#define CH4_SWITCH_THRESHOLD_US_DEFAULT 1500U

/* Manual DEPLOY mode. deploy_servo_us is the servo pulse the steering servo is
 * slewed to and held at while the motor is forced off (raise the motor). It
 * lives in the full servo electrical band (500..2500 us); default 2167 us is
 * the ~180 deg "up" endpoint. */
#define DEPLOY_SERVO_US_MIN 500U
#define DEPLOY_SERVO_US_MAX 2500U
#define DEPLOY_SERVO_US_DEFAULT 2167U

/* CH4 click-gesture window: max gap (ms) between clicks of one burst. Above it
 * the burst closes (1 click -> arm/disarm/stow, 3 clicks -> deploy/stow). */
#define CLICK_WINDOW_MS_MIN 200U
#define CLICK_WINDOW_MS_MAX 1000U
#define CLICK_WINDOW_MS_DEFAULT 500U
