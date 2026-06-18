#include "click_counter.h"

/* Number of clicks that constitutes a TRIPLE gesture (reported immediately). */
#define CLICK_TRIPLE_COUNT 3U

void click_counter_reset(click_counter_state *st)
{
    st->count = 0U;
    st->frames_since_last = 0U;
}

/* A click landed: extend the open burst. The third click completes a TRIPLE
 * immediately and resets the burst; fewer keep accumulating (no gesture yet). */
static click_gesture on_click(click_counter_state *st)
{
    st->count++;
    st->frames_since_last = 0U;
    if (st->count >= CLICK_TRIPLE_COUNT) {
        click_counter_reset(st);
        return CLICK_TRIPLE;
    }
    return CLICK_NONE;
}

/* No click this frame: advance the silence counter on an open burst and, once
 * the window elapses, close it. A closed 1-click burst is a SINGLE; 2 clicks
 * resolve to nothing (CLICK_NONE). */
static click_gesture on_silence(click_counter_state *st, uint16_t window_frames)
{
    if (st->count == 0U) {
        return CLICK_NONE; /* idle */
    }
    st->frames_since_last++;
    if (st->frames_since_last < window_frames) {
        return CLICK_NONE; /* still inside the window */
    }
    uint8_t closed = st->count;
    click_counter_reset(st);
    return closed == 1U ? CLICK_SINGLE : CLICK_NONE;
}

click_gesture click_counter_update(click_counter_state *st, bool click_this_frame,
                                   uint16_t window_frames)
{
    if (click_this_frame) {
        return on_click(st);
    }
    return on_silence(st, window_frames);
}
