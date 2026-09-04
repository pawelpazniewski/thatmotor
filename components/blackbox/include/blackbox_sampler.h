#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure (framework-agnostic) recording decision for the blackbox recorder.
 *
 * The recorder task peeks the control-loop snapshot every BLACKBOX_TICK_MS and
 * turns it into one recording action. This is the whole policy, factored out so
 * it is host-testable with no ESP-IDF dependency: the task is a thin adapter
 * that calls this, fills the record fields, and does flash I/O.
 *
 * Session model (unchanged): a session spans one contiguous non-OFF run of the
 * spot-lock substate and includes PAUSED (R2). OFF -> non-OFF opens a session
 * (header + first sample); non-OFF stays inside it.
 *
 * Adaptive rate (new): inside a running session a sample is written only when
 * something changed -- a discrete event, or the position error moved by at least
 * BLACKBOX_ERR_DELTA_M -- and otherwise at most once per BLACKBOX_HEARTBEAT_MS.
 * A boat holding station still is therefore sparse; a moving boat is dense. This
 * captures far more meaningful data per flash byte than a fixed rate.
 *
 * OFF tail (new): the old model stopped the instant the hold dropped to OFF, so
 * a drift after the operator took over (stick nudge / disarm) was invisible.
 * Now the drop and the next BLACKBOX_OFF_TAIL_SAMPLES ticks are recorded as tail
 * samples (carrying the end reason and the post-override drift) before logging
 * stops. Bounded, so manual driving does not fill the ring.
 *
 * Entry attempts (new): a CH3 rising edge that fails the entry gate leaves
 * substate at OFF the whole time, so the transition logic above never fires for
 * it -- previously a rejected press left no trace at all. The recorder carries a
 * monotonic `attempt_seq` (bumped by loop_step on every CH3 edge, success or
 * not) alongside `last_attempt_seq` (the value last acted on); once OFF has
 * fully settled (no session, no tail owed) a new attempt_seq is logged as
 * BLACKBOX_ACTION_LOG_ATTEMPT. A SUCCESSFUL attempt is already visible as the
 * header+sample pair START_SESSION writes, so this only ever fires for a
 * rejected one.
 *
 * spot_lock substate values mirror control_loop_snapshot.spot_lock_state:
 * 0 = OFF, 1 = ACTIVE, 2 = PAUSED. "non-OFF" means ACTIVE or PAUSED.
 */

/* Spot-lock substate encoding, shared with control_loop_snapshot.spot_lock_state. */
#define BLACKBOX_SPOT_LOCK_OFF 0U
#define BLACKBOX_SPOT_LOCK_ACTIVE 1U
#define BLACKBOX_SPOT_LOCK_PAUSED 2U

/* Recorder tick period; the sampler policy is expressed against it. */
#define BLACKBOX_TICK_MS 100U
/* Idle heartbeat: a still hold still writes at least one sample this often. */
#define BLACKBOX_HEARTBEAT_MS 2000U
/* Position-error change (metres) that forces a sample inside a session. */
#define BLACKBOX_ERR_DELTA_M 2U
/* Tail samples written after the hold drops to OFF (~3 s at 100 ms/tick). */
#define BLACKBOX_OFF_TAIL_SAMPLES 30U

/** Recording action the task must take this tick. */
typedef enum {
    BLACKBOX_ACTION_IDLE = 0,          /* nothing to record this tick */
    BLACKBOX_ACTION_START_SESSION = 1, /* OFF -> non-OFF: header + first sample */
    BLACKBOX_ACTION_SAMPLE = 2,        /* append an in-session sample */
    BLACKBOX_ACTION_SAMPLE_TAIL = 3,   /* append a post-OFF tail sample (drift+reason) */
    BLACKBOX_ACTION_LOG_ATTEMPT = 4,   /* append a rejected CH3 entry attempt */
} blackbox_sampler_action;

/** One tick's inputs. All fields are recorder-supplied; the sampler is pure. */
typedef struct {
    uint8_t prev_substate;   /* spot-lock substate on the previous tick */
    uint8_t cur_substate;    /* spot-lock substate this tick */
    uint16_t ms_since_sample;/* elapsed since the last written record */
    uint16_t err_m;          /* current position error to target, metres */
    uint16_t last_err_m;     /* error at the last written sample */
    bool event;              /* a discrete change occurred -> force a sample */
    uint16_t off_tail_left;  /* tail samples still owed (recorder-carried state) */
    uint32_t attempt_seq;      /* current spot_lock_attempt_seq from the snapshot */
    uint32_t last_attempt_seq; /* attempt_seq already acted on (recorder-carried) */
} blackbox_sampler_in;

/** One tick's decision plus the carry-state for the next tick. */
typedef struct {
    blackbox_sampler_action action;
    uint16_t off_tail_left;    /* store back; feed as in.off_tail_left next tick */
    uint32_t last_attempt_seq; /* store back; feed as in.last_attempt_seq next tick */
} blackbox_sampler_out;

/**
 * Decide this tick's action from the substate transition, the adaptive-rate
 * inputs, and the carried tail counter.
 *
 * @param in  Tick inputs (must be non-NULL).
 * @return The action to take and the updated tail counter.
 */
blackbox_sampler_out blackbox_sampler_step(const blackbox_sampler_in *in);

#ifdef __cplusplus
}
#endif
