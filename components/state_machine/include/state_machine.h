#pragma once

#include <stdbool.h>

#include "signal_chain.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure control state machine (Unit 6).
 *
 * Four states, no I/O, no globals: a single transition function maps the
 * current state plus a snapshot of inputs to the next state plus per-state
 * actuator target overrides (consumed by the signal chains in Unit 5/7).
 *
 * Safety invariants enforced here:
 *  - Boot always lands in DISARMED (SI-1); the reset reason is only logged.
 *  - DISARMED->ARMED is gated (R7): RC valid AND throttle neutral AND an
 *    explicit arm request AND no calibration/settings-apply in progress.
 *  - Any state with RC invalid drops to FAILSAFE (SI-4/R6).
 *  - FAILSAFE is latched: it persists while RC is invalid and the ONLY exit is
 *    RC recovery -> DISARMED. It NEVER transitions straight back to ARMED.
 *  - The servo rule is independent of arming: RC valid -> servo tracks CH1,
 *    RC invalid -> servo centers, regardless of ARMED/DISARMED.
 *  - DISARMED->ESC_CALIBRATION is gated (R15/SI-5): RC valid AND throttle
 *    neutral AND an explicit calibration request AND a confirmed warning. It
 *    NEVER starts automatically. RC loss inside calibration drops to FAILSAFE.
 */

/** Control states. ESC_CALIBRATION is a first-class state (sequence in Unit 9). */
typedef enum {
    SM_STATE_DISARMED = 0,
    SM_STATE_ARMED = 1,
    SM_STATE_FAILSAFE = 2,
    SM_STATE_ESC_CALIBRATION = 3,
} sm_state;

/**
 * Why an arm request would be refused, for the panel to surface (R7 gate). The
 * order mirrors the can_arm guard exactly, so the FIRST unmet condition wins;
 * SM_ARM_READY means every condition holds and a fresh arm intent would arm.
 */
typedef enum {
    SM_ARM_READY = 0,                /* all arm conditions satisfied */
    SM_ARM_NO_RC = 1,                /* RC signal invalid */
    SM_ARM_THROTTLE_NOT_NEUTRAL = 2, /* throttle stick off the neutral band */
    SM_ARM_CALIBRATING = 3,          /* ESC calibration sequence running */
    SM_ARM_SETTINGS_APPLYING = 4,    /* a settings apply is mid-flight */
} sm_arm_reason;

/**
 * Snapshot of the decision inputs for one transition. Booleans are debounced /
 * resolved upstream (rc_validity, UI edge detection); this struct carries no
 * timing of its own.
 */
typedef struct {
    bool rc_valid;                  /* debounced RC_valid (CH1 AND CH2) */
    bool throttle_neutral;          /* throttle stick within neutral band */
    bool calib_in_progress;         /* ESC calibration sequence running */
    bool settings_apply_in_progress;/* a pending settings apply is mid-flight */
    bool ui_arm_request;            /* explicit arm action from the panel */
    bool ui_disarm_request;         /* explicit disarm action from the panel */
    bool ui_calib_request;          /* explicit "start ESC calibration" action */
    bool ui_calib_confirm;          /* operator confirmed the removal warning */
} sm_inputs;

/**
 * Result of one transition: the next state plus the target-override modes the
 * signal chains must apply this cycle.
 */
typedef struct {
    sm_state state;
    throttle_target_mode throttle_target;
    servo_target_mode servo_target;
} sm_outputs;

/**
 * Compute one state transition (pure).
 *
 * @param current  Current state.
 * @param inputs   Decision inputs for this cycle (must be non-NULL).
 * @return The next state and the per-state actuator target overrides.
 */
sm_outputs sm_step(sm_state current, const sm_inputs *inputs);

/**
 * Report why arming would be blocked (pure), independent of the current state.
 * Evaluates the SAME conditions, in the SAME priority order, as the can_arm
 * guard: the first unmet condition is returned, or SM_ARM_READY when all hold.
 * Does NOT consider arm intent (it answers "could an arm intent succeed now?").
 *
 * @param inputs  Decision inputs for this cycle (must be non-NULL).
 * @return The first blocking reason, or SM_ARM_READY if none.
 */
sm_arm_reason sm_arm_block_reason(const sm_inputs *inputs);

#ifdef __cplusplus
}
#endif
