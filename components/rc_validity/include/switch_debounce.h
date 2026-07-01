#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rc_sample.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure RC-channel-as-position-switch interpreter (framework-agnostic, no IDF).
 *
 * Generic over any auxiliary RC channel read as a switch. Two instances exist:
 *   - CH4 (GPIO32): momentary TOGGLE button feeding the click_counter (arm /
 *     disarm / deploy gestures).
 *   - CH3 (GPIO8):  spot-lock ON/OFF switch (the TO_HIGH edge enters spot-lock,
 *     the TO_LOW edge / held-low aborts it).
 * Both channels are deliberately OUTSIDE RC_valid (R12): a lost aux channel must
 * never trip failsafe.
 *
 * Determinism / timing: this module owns NO clock. It works purely per frame
 * (one switch_debounce_update call per control cycle). Debounce is expressed in
 * frames, not milliseconds, so it is reproducible in host tests with no time
 * source and immune to tick-counter wrap.
 *
 * Semantics:
 *  - "high"     := the pulse is inside the sanity band AND its width is
 *                  >= threshold_us. "low" := in band AND width < threshold.
 *                  Out-of-band / no edge seen -> level held (no change, NONE):
 *                  a floating pin never fabricates an edge.
 *  - debounce   := a candidate level must persist debounce_frames consecutive
 *                  frames before it is accepted as the new stable level.
 *  - event      := SWITCH_DEBOUNCE_TO_HIGH on the accepted low->high transition,
 *                  SWITCH_DEBOUNCE_TO_LOW on the accepted high->low transition,
 *                  SWITCH_DEBOUNCE_NONE otherwise. Holding repeats nothing.
 *  - init       := the FIRST accepted stable level only establishes a baseline
 *                  (NONE), so powering up with the switch already high does NOT
 *                  fire an edge (no auto-arm / auto-enter on boot).
 */

/* Consecutive control frames a level must hold before it is accepted. At the
 * ~50 Hz control rate, 3 frames ~= 60 ms: long enough to reject a single glitch
 * frame, short enough to feel instant. Lives here (not in settings) so both the
 * pure module and its HAL callers share one definition without crossing the
 * settings component's private boundary. */
#define SWITCH_DEBOUNCE_FRAMES 3U

/** Directional edge event from a position switch for one frame. */
typedef enum {
    SWITCH_DEBOUNCE_NONE = 0,    /* no accepted level change this frame */
    SWITCH_DEBOUNCE_TO_HIGH = 1, /* accepted low->high edge */
    SWITCH_DEBOUNCE_TO_LOW = 2,  /* accepted high->low edge */
} switch_debounce_event;

/** Configuration for one switch interpretation (all constant per build). */
typedef struct {
    uint16_t threshold_us;     /* width_us >= this (and in band) -> high */
    uint16_t sanity_min_us;    /* lower sanity bound (e.g. RC_US_MIN) */
    uint16_t sanity_max_us;    /* upper sanity bound (e.g. RC_US_MAX) */
    uint8_t debounce_frames;   /* consecutive frames a level must hold */
} switch_debounce_cfg;

/**
 * Persistent debounce / edge-detection state for one switch. Carries no timing
 * of its own; updated in place every frame.
 *
 * is_high       Current accepted stable level (true = switch high).
 * candidate     The level seen this frame that is being counted toward accept.
 * stable_frames Consecutive frames the candidate has matched.
 * initialised   True once a baseline level has been established (suppresses the
 *               very first event).
 */
typedef struct {
    bool is_high;
    bool candidate;
    uint8_t stable_frames;
    bool initialised;
} switch_debounce_state;

/**
 * Initialise a switch state to "low, uninitialised".
 *
 * @param st  State to initialise (must be non-NULL).
 */
void switch_debounce_init(switch_debounce_state *st);

/**
 * Advance the position-switch interpreter by one frame.
 *
 * @param st      Persistent state, updated in place (must be non-NULL).
 * @param sample  Latest raw RC capture sample for the channel (must be non-NULL).
 * @param cfg     Switch configuration (must be non-NULL).
 * @return SWITCH_DEBOUNCE_TO_HIGH on the accepted low->high edge,
 *         SWITCH_DEBOUNCE_TO_LOW on the accepted high->low edge,
 *         SWITCH_DEBOUNCE_NONE otherwise (including the initial baseline frame,
 *         a held position, and any out-of-band / no-edge frame).
 */
switch_debounce_event switch_debounce_update(switch_debounce_state *st,
                                             const rc_channel_sample *sample,
                                             const switch_debounce_cfg *cfg);

#ifdef __cplusplus
}
#endif
