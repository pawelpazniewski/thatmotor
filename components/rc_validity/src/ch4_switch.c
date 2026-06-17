#include "ch4_switch.h"

/* Resolve the raw "pressed" level for this frame from the CH4 sample: it counts
 * as pressed only when an edge has been seen, the pulse is inside the sanity
 * band, and its width is at/above the configured threshold. Anything else
 * (floating pin out of band, no pulse yet) is treated as released. */
static bool raw_pressed(const rc_channel_sample *ch4, const ch4_switch_cfg *cfg)
{
    if (!ch4->edge_seen) {
        return false;
    }
    if (ch4->width_us < cfg->sanity_min_us || ch4->width_us > cfg->sanity_max_us) {
        return false;
    }
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
    st->pressed = false;
    st->candidate = false;
    st->stable_frames = 0U;
    st->initialised = false;
}

bool ch4_switch_update(ch4_switch_state *st, const rc_channel_sample *ch4,
                       const ch4_switch_cfg *cfg)
{
    bool level = raw_pressed(ch4, cfg);
    uint8_t debounce_frames =
        cfg->debounce_frames == 0U ? 1U : cfg->debounce_frames;

    if (!level_is_stable(st, level, debounce_frames)) {
        return false;
    }

    /* The first stable level only seeds the baseline (no pulse) so booting with
     * the button already held does not toggle. */
    if (!st->initialised) {
        st->initialised = true;
        st->pressed = level;
        return false;
    }

    if (level == st->pressed) {
        return false; /* level unchanged (held / steady) -> no edge */
    }
    bool rising = level && !st->pressed;
    st->pressed = level;
    return rising; /* toggle pulse ONLY on released -> pressed */
}
