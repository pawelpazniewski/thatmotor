#include "commit_debounce.h"

#include <stdbool.h>
#include <stdint.h>

static uint32_t clamp_window(uint32_t debounce_ms)
{
    if (debounce_ms < COMMIT_DEBOUNCE_MIN_MS) {
        return COMMIT_DEBOUNCE_MIN_MS;
    }
    if (debounce_ms > COMMIT_DEBOUNCE_MAX_MS) {
        return COMMIT_DEBOUNCE_MAX_MS;
    }
    return debounce_ms;
}

void commit_debounce_init(commit_debounce_state *state, uint32_t debounce_ms)
{
    state->debounce_ms = clamp_window(debounce_ms);
    state->dirty = false;
    state->last_change_ms = 0;
}

void commit_debounce_mark_changed(commit_debounce_state *state, uint32_t now_ms)
{
    state->dirty = true;
    state->last_change_ms = now_ms;
}

bool commit_debounce_should_commit(const commit_debounce_state *state,
                                   uint32_t now_ms, bool force)
{
    if (!state->dirty) {
        return false;
    }
    if (force) {
        return true;
    }
    /* Modular subtraction is wrap-safe over the monotonic ms counter. */
    uint32_t elapsed = now_ms - state->last_change_ms;
    return elapsed >= state->debounce_ms;
}

void commit_debounce_mark_committed(commit_debounce_state *state)
{
    state->dirty = false;
}
