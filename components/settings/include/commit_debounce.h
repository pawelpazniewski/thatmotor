#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure (framework-agnostic) delayed-commit logic for the NVS store.
 *
 * Coalesces a burst of parameter changes into a single flash write so dragging
 * a slider does not burn flash cycles or lag the loop: a change starts (or
 * restarts) a debounce window, and a commit is due only once the window has
 * elapsed with no further change, or immediately on an explicit "Save" (force).
 *
 * Time is injected as a monotonic millisecond counter (now_ms); this module
 * holds no real clock, so it is fully host-testable. The orchestration layer
 * (control_loop) additionally gates the actual commit on DISARMED (R17); this
 * module only answers "is a commit due yet?".
 */

/* Allowed debounce window, milliseconds (plan: 2-5 s). */
#define COMMIT_DEBOUNCE_MIN_MS 2000U
#define COMMIT_DEBOUNCE_MAX_MS 5000U
#define COMMIT_DEBOUNCE_DEFAULT_MS 3000U

/** Carry-over debounce state. Treat as opaque; mutate only via the API below. */
typedef struct {
    uint32_t debounce_ms;     /* configured window */
    bool dirty;               /* an un-committed change is pending */
    uint32_t last_change_ms;  /* now_ms of the most recent change */
} commit_debounce_state;

/**
 * Initialise the debounce state: clean (nothing pending) with the given window
 * clamped into [COMMIT_DEBOUNCE_MIN_MS, COMMIT_DEBOUNCE_MAX_MS].
 *
 * @param state        State to initialise (must be non-NULL).
 * @param debounce_ms  Requested window in ms (clamped to the allowed range).
 */
void commit_debounce_init(commit_debounce_state *state, uint32_t debounce_ms);

/**
 * Record a parameter change at now_ms. Marks dirty and (re)starts the window,
 * so a later change pushes the due time out (no commit mid-burst).
 *
 * @param state   Debounce state (must be non-NULL).
 * @param now_ms  Current monotonic time in ms.
 */
void commit_debounce_mark_changed(commit_debounce_state *state, uint32_t now_ms);

/**
 * Whether a commit is due at now_ms: dirty AND (force OR the window elapsed
 * since the last change). Pure query, does NOT clear the dirty flag.
 *
 * @param state   Debounce state (must be non-NULL).
 * @param now_ms  Current monotonic time in ms.
 * @param force   True for an explicit "Save" (commit immediately if dirty).
 * @return true when the caller should commit now.
 */
bool commit_debounce_should_commit(const commit_debounce_state *state,
                                   uint32_t now_ms, bool force);

/**
 * Clear the dirty flag after a successful commit. Call only once the write has
 * actually landed, so a failed write leaves the change pending for retry.
 *
 * @param state  Debounce state (must be non-NULL).
 */
void commit_debounce_mark_committed(commit_debounce_state *state);

#ifdef __cplusplus
}
#endif
