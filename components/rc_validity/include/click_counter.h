#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure click-gesture counter (framework-agnostic, no IDF includes).
 *
 * A monostabile CH4 button fires exactly one edge ("click") per press. This
 * module accumulates clicks inside a sliding window and classifies a completed
 * burst as a SINGLE (1 click) or TRIPLE (3 clicks) gesture; 2 clicks resolve to
 * nothing. The TRIPLE is reported the instant the third click lands (no need to
 * wait for the window) so a deliberate triple-press feels immediate; SINGLE can
 * only be known after the window elapses with no further click (so a first
 * click is not prematurely treated as SINGLE while a second/third may follow).
 *
 * Determinism / timing: this module owns NO clock. It works purely per frame
 * (one click_counter_update call per control cycle); the window is expressed in
 * frames, not milliseconds, so it is reproducible in host tests with no time
 * source and immune to tick-counter wrap. The caller converts its configured
 * window in milliseconds to frames once.
 */

/** Completed click gesture classified this frame. */
typedef enum {
    CLICK_NONE = 0,   /* nothing resolved this frame (idle / accumulating) */
    CLICK_SINGLE = 1, /* a 1-click burst completed (window elapsed) */
    CLICK_TRIPLE = 2, /* a 3-click burst completed (on the third click) */
} click_gesture;

/**
 * Persistent click-counter state. Carries no timing of its own; updated in
 * place every frame.
 *
 * count             Clicks accumulated in the current open burst (0 = idle).
 * frames_since_last Frames since the last click while a burst is open.
 */
typedef struct {
    uint8_t count;
    uint16_t frames_since_last;
} click_counter_state;

/**
 * Reset a click-counter state to idle (no open burst).
 *
 * @param st  State to reset (must be non-NULL).
 */
void click_counter_reset(click_counter_state *st);

/**
 * Advance the click counter by one frame.
 *
 * @param st               Persistent state, updated in place (must be non-NULL).
 * @param click_this_frame True if a click (CH4 edge) landed this frame.
 * @param window_frames    Frames of silence that close an open burst.
 * @return CLICK_TRIPLE the instant a third click lands; CLICK_SINGLE when a
 *         single-click burst closes after window_frames of silence; CLICK_NONE
 *         otherwise (idle, mid-burst, or a closed 2-click burst).
 */
click_gesture click_counter_update(click_counter_state *st, bool click_this_frame,
                                   uint16_t window_frames);

#ifdef __cplusplus
}
#endif
