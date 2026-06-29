#include "switch_debounce.h"

/* Resolve the raw "high" level for this frame from the sample: the switch reads
 * high only when an edge has been seen, the pulse is inside the sanity band, and
 * its width is at/above the configured threshold. Out-of-band / no pulse yet is
 * reported as "no reading" via *has_reading so the caller can hold the previous
 * level instead of fabricating a spurious edge from a floating pin. */
static bool raw_high(const rc_channel_sample *sample,
                     const switch_debounce_cfg *cfg, bool *has_reading)
{
    if (!sample->edge_seen ||
        sample->width_us < cfg->sanity_min_us ||
        sample->width_us > cfg->sanity_max_us) {
        *has_reading = false;
        return false;
    }
    *has_reading = true;
    return sample->width_us >= cfg->threshold_us;
}

/* Advance the debounce counter for this frame's raw level and report whether the
 * level has now held for debounce_frames consecutive frames (i.e. it is stable
 * this frame). The candidate restarts the counter whenever the raw level flips. */
static bool level_is_stable(switch_debounce_state *st, bool level,
                            uint8_t debounce_frames)
{
    if (level != st->candidate) {
        st->candidate = level;
        st->stable_frames = 1U;
    } else if (st->stable_frames < debounce_frames) {
        st->stable_frames++;
    }
    return st->stable_frames >= debounce_frames;
}

void switch_debounce_init(switch_debounce_state *st)
{
    st->is_high = false;
    st->candidate = false;
    st->stable_frames = 0U;
    st->initialised = false;
}

switch_debounce_event switch_debounce_update(switch_debounce_state *st,
                                             const rc_channel_sample *sample,
                                             const switch_debounce_cfg *cfg)
{
    bool has_reading = false;
    bool level = raw_high(sample, cfg, &has_reading);
    if (!has_reading) {
        /* No valid pulse: hold the last accepted level, never invent an edge. */
        st->candidate = st->is_high;
        st->stable_frames = 0U;
        return SWITCH_DEBOUNCE_NONE;
    }

    uint8_t debounce_frames =
        cfg->debounce_frames == 0U ? 1U : cfg->debounce_frames;
    if (!level_is_stable(st, level, debounce_frames)) {
        return SWITCH_DEBOUNCE_NONE;
    }

    /* The first stable level only seeds the baseline (no event) so booting with
     * the switch already high does not fire an edge. */
    if (!st->initialised) {
        st->initialised = true;
        st->is_high = level;
        return SWITCH_DEBOUNCE_NONE;
    }

    if (level == st->is_high) {
        return SWITCH_DEBOUNCE_NONE; /* level unchanged (held) -> no edge */
    }
    st->is_high = level;
    return level ? SWITCH_DEBOUNCE_TO_HIGH : SWITCH_DEBOUNCE_TO_LOW;
}
