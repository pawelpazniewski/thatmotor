#include "blackbox_sampler.h"

/* A substate is "off" only for the OFF value; ACTIVE and PAUSED are both live
 * (a session spans a contiguous non-OFF run and includes PAUSED, R2). */
static bool is_off(uint8_t substate)
{
    return substate == BLACKBOX_SPOT_LOCK_OFF;
}

/* |a - b| >= thr without signed intermediates (both are uint16 telemetry). */
static bool abs_diff_ge(uint16_t a, uint16_t b, uint16_t thr)
{
    uint16_t d = (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
    return d >= thr;
}

/* Inside a running session, write a sample only when something changed: a
 * discrete event, the error moved by at least the delta, or the idle heartbeat
 * elapsed. Otherwise skip this tick (adaptive rate). */
static bool sample_due(const blackbox_sampler_in *in)
{
    return in->event ||
           abs_diff_ge(in->err_m, in->last_err_m, BLACKBOX_ERR_DELTA_M) ||
           in->ms_since_sample >= BLACKBOX_HEARTBEAT_MS;
}

blackbox_sampler_out blackbox_sampler_step(const blackbox_sampler_in *in)
{
    blackbox_sampler_out out = {BLACKBOX_ACTION_IDLE, in->off_tail_left,
                                in->attempt_seq};
    bool prev_off = is_off(in->prev_substate);
    bool cur_off = is_off(in->cur_substate);

    if (!prev_off && !cur_off) {
        /* Running session: adaptive-rate in-session sample. A substate change
         * within the session (ACTIVE<->PAUSED) is always worth a sample,
         * independent of whatever event flag the recorder passed. */
        bool substate_changed = in->prev_substate != in->cur_substate;
        out.action = (substate_changed || sample_due(in))
                         ? BLACKBOX_ACTION_SAMPLE
                         : BLACKBOX_ACTION_IDLE;
        return out;
    }
    if (prev_off && !cur_off) {
        /* Leading edge of a session: header + first sample. Clear any stale
         * tail owed from a previous session that never fully drained. */
        out.action = BLACKBOX_ACTION_START_SESSION;
        out.off_tail_left = 0U;
        return out;
    }
    if (!prev_off && cur_off) {
        /* The hold just dropped to OFF: record the drop plus a bounded tail so
         * the post-override drift and its reason are captured. This tick spends
         * one of the tail budget. */
        out.action = BLACKBOX_ACTION_SAMPLE_TAIL;
        out.off_tail_left = BLACKBOX_OFF_TAIL_SAMPLES - 1U;
        return out;
    }
    /* OFF -> OFF: keep draining the tail budget, then a rejected CH3 entry
     * attempt (new attempt_seq since the last one acted on), then fall silent.
     * out.last_attempt_seq is already latched to in->attempt_seq above, so this
     * fires at most once per attempt regardless of which branch runs later. */
    if (in->off_tail_left > 0U) {
        out.action = BLACKBOX_ACTION_SAMPLE_TAIL;
        out.off_tail_left = (uint16_t)(in->off_tail_left - 1U);
        return out;
    }
    if (in->attempt_seq != in->last_attempt_seq) {
        out.action = BLACKBOX_ACTION_LOG_ATTEMPT;
    }
    return out;
}
