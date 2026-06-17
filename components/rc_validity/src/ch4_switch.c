#include "ch4_switch.h"

/* Resolve the raw "high" level for this frame from the CH4 sample: the switch
 * reads high only when an edge has been seen, the pulse is inside the sanity
 * band, and its width is at/above the configured threshold. Out-of-band / no
 * pulse yet is reported as "no reading" via *has_reading so the caller can hold
 * the previous level instead of fabricating a spurious edge from a floating
 * pin. */
static bool raw_high(const rc_channel_sample *ch4, const ch4_switch_cfg *cfg,
                     bool *has_reading)
{
    if (!ch4->edge_seen ||
        ch4->width_us < cfg->sanity_min_us ||
        ch4->width_us > cfg->sanity_max_us) {
        *has_reading = false;
        return false;
    }
    *has_reading = true;
    return ch4->width_us >= cfg->threshold_us;
}

/* Advance the debounce counter for this frame's raw level and report whether the
 * level has now held for debounce_frames consecutive frames (i.e. it is stable
 * this frame). The candidate restarts the counter whenever the raw level flips. */
static bool level_is_stable(ch4_switch_state *st, bool level,
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

void ch4_switch_init(ch4_switch_state *st)
{
    st->is_high = false;
    st->candidate = false;
    st->stable_frames = 0U;
    st->initialised = false;
}

ch4_switch_event ch4_switch_update(ch4_switch_state *st,
                                   const rc_channel_sample *ch4,
                                   const ch4_switch_cfg *cfg)
{
    bool has_reading = false;
    bool level = raw_high(ch4, cfg, &has_reading);
    if (!has_reading) {
        /* No valid pulse: hold the last accepted level, never invent an edge. */
        st->candidate = st->is_high;
        st->stable_frames = 0U;
        return CH4_SWITCH_NONE;
    }

    uint8_t debounce_frames =
        cfg->debounce_frames == 0U ? 1U : cfg->debounce_frames;
    if (!level_is_stable(st, level, debounce_frames)) {
        return CH4_SWITCH_NONE;
    }

    /* The first stable level only seeds the baseline (no event) so booting with
     * the switch already high does not arm. */
    if (!st->initialised) {
        st->initialised = true;
        st->is_high = level;
        return CH4_SWITCH_NONE;
    }

    if (level == st->is_high) {
        return CH4_SWITCH_NONE; /* level unchanged (held) -> no edge */
    }
    st->is_high = level;
    return level ? CH4_SWITCH_TO_HIGH : CH4_SWITCH_TO_LOW;
}
