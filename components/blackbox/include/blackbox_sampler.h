#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure (framework-agnostic) session/sample decision for the blackbox recorder.
 *
 * The recorder task peeks the control-loop snapshot at ~2 Hz and needs to turn
 * the spot-lock substate transition (prev -> cur) into one recording action.
 * This is the whole decision, factored out so it is host-testable with no
 * ESP-IDF dependency (no esp_ or driver includes): the task is a thin adapter
 * that calls this and then does flash I/O.
 *
 * spot_lock substate values mirror control_loop_snapshot.spot_lock_state:
 * 0 = OFF, 1 = ACTIVE, 2 = PAUSED. "non-OFF" means ACTIVE or PAUSED. A session
 * spans one contiguous non-OFF run and includes PAUSED (R2): pausing does not
 * end the session, it is still sampled.
 *
 *   OFF     -> non-OFF : START_SESSION (emit a session header)
 *   non-OFF -> non-OFF : SAMPLE        (append a sample; incl. ACTIVE -> PAUSED)
 *   non-OFF -> OFF     : CLOSE_SESSION (stop; no further samples)
 *   OFF     -> OFF     : IDLE          (nothing to record)
 */

/* Spot-lock substate encoding, shared with control_loop_snapshot.spot_lock_state. */
#define BLACKBOX_SPOT_LOCK_OFF 0U
#define BLACKBOX_SPOT_LOCK_ACTIVE 1U
#define BLACKBOX_SPOT_LOCK_PAUSED 2U

/** Recording action the task must take this tick. */
typedef enum {
    BLACKBOX_ACTION_IDLE = 0,          /* OFF -> OFF: do nothing */
    BLACKBOX_ACTION_START_SESSION = 1, /* OFF -> non-OFF: write session header */
    BLACKBOX_ACTION_SAMPLE = 2,        /* non-OFF -> non-OFF: append a sample */
    BLACKBOX_ACTION_CLOSE_SESSION = 3, /* non-OFF -> OFF: close the session */
} blackbox_sampler_action;

/**
 * Decide the recording action from the substate transition.
 *
 * @param prev_substate  Spot-lock substate on the previous tick.
 * @param cur_substate   Spot-lock substate on this tick.
 * @return The action to take (see blackbox_sampler_action).
 */
blackbox_sampler_action blackbox_sampler_decide(uint8_t prev_substate,
                                                uint8_t cur_substate);

#ifdef __cplusplus
}
#endif
