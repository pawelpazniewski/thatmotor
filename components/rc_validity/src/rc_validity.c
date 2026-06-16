#include "rc_validity.h"

#include <stdbool.h>
#include <stdint.h>

#include "cap_math.h"

/* Absolute difference for unsigned timestamps/periods. */
static uint32_t abs_diff_u32(uint32_t a, uint32_t b)
{
    return a > b ? a - b : b - a;
}

static bool width_in_range(const rc_channel_sample *sample,
                           const rc_channel_cfg *cfg)
{
    return sample->width_us >= cfg->width_min_us &&
           sample->width_us <= cfg->width_max_us;
}

static bool period_in_tolerance(const rc_channel_sample *sample,
                                const rc_channel_cfg *cfg)
{
    return abs_diff_u32(sample->period_us, cfg->period_expected_us) <=
           cfg->period_tol_us;
}

static bool edge_recent(const rc_channel_sample *sample, uint32_t now_ticks,
                        const rc_channel_cfg *cfg)
{
    /* Wrap-safe: elapsed is computed by unsigned modular subtraction in the
     * 32-bit capture-tick domain (cap_ticks_elapsed), so it stays correct
     * across a counter wrap, then is converted to microseconds for comparison
     * against the human-readable timeout. */
    uint32_t elapsed_ticks = cap_ticks_elapsed(now_ticks, sample->last_edge_ticks);
    return cap_ticks_to_us(elapsed_ticks) <= cfg->edge_timeout_us;
}

bool channel_valid(const rc_channel_sample *sample, uint32_t now_ticks,
                   const rc_channel_cfg *cfg)
{
    if (!sample->edge_seen) {
        return false;
    }
    if (!edge_recent(sample, now_ticks, cfg)) {
        return false;
    }
    if (!width_in_range(sample, cfg)) {
        return false;
    }
    return period_in_tolerance(sample, cfg);
}

bool rc_valid(bool ch1_valid, bool ch2_valid)
{
    return ch1_valid && ch2_valid;
}

void rc_debounce_init(rc_debounce_state *state, uint16_t threshold)
{
    state->bad_frame_count = 0;
    state->invalid_threshold = threshold;
    state->latched_valid = true;
}

bool rc_debounce_update(rc_debounce_state *state, bool frame_valid)
{
    if (frame_valid) {
        state->bad_frame_count = 0;
        state->latched_valid = true;
        return state->latched_valid;
    }

    if (state->bad_frame_count < UINT16_MAX) {
        state->bad_frame_count++;
    }
    if (state->bad_frame_count >= state->invalid_threshold) {
        state->latched_valid = false;
    }
    return state->latched_valid;
}
