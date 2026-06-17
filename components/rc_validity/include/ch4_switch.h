#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rc_sample.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure CH4-as-position-switch interpreter (framework-agnostic, no IDF includes).
 *
 * CH4 (GPIO32) is read by rc_capture as a normal RC channel but here it is
 * interpreted as a 2-position toggle switch whose POSITION maps to intent: each
 * switch movement is one state change. The interpreter emits a directional edge
 * event so the caller can drive arm/disarm intent (high = arm, low = disarm):
 * one flick of the switch = one state change (no "double toggle" needed).
 *
 * Determinism / timing: this module owns NO clock. It works purely per frame
 * (one ch4_switch_update call per control cycle). Debounce is expressed in
 * frames, not milliseconds, so it is reproducible in host tests with no time
 * source and immune to tick-counter wrap.
 *
 * Semantics:
 *  - "high"     := the CH4 pulse is inside the sanity band AND its width is
 *                  >= threshold_us. "low" := in band AND width < threshold.
 *                  Out-of-band / no edge seen -> level held (no change, NONE):
 *                  a floating pin never fabricates an edge.
 *  - debounce   := a candidate level must persist debounce_frames consecutive
 *                  frames before it is accepted as the new stable level.
 *  - event      := CH4_SWITCH_TO_HIGH on the accepted low->high transition,
 *                  CH4_SWITCH_TO_LOW on the accepted high->low transition,
 *                  CH4_SWITCH_NONE otherwise. Holding a position repeats nothing.
 *  - init       := the FIRST accepted stable level only establishes a baseline
 *                  (NONE), so powering up with the switch already high does NOT
 *                  arm.
 */

/* Consecutive control frames a CH4 level must hold before it is accepted. At
 * the ~50 Hz control rate, 3 frames ~= 60 ms: long enough to reject a single
 * glitch frame, short enough to feel instant. Lives here (not in settings) so
 * both the pure module and its HAL caller share one definition without crossing
 * the settings component's private boundary. */
#define CH4_SWITCH_DEBOUNCE_FRAMES 3U

/** Directional edge event from the CH4 position switch for one frame. */
typedef enum {
    CH4_SWITCH_NONE = 0,    /* no accepted level change this frame */
    CH4_SWITCH_TO_HIGH = 1, /* accepted low->high edge (arm intent) */
    CH4_SWITCH_TO_LOW = 2,  /* accepted high->low edge (disarm intent) */
} ch4_switch_event;

/** Configuration for the CH4 switch interpretation (all constant per build). */
typedef struct {
    uint16_t threshold_us;     /* width_us >= this (and in band) -> high */
    uint16_t sanity_min_us;    /* lower sanity bound (e.g. RC_US_MIN) */
    uint16_t sanity_max_us;    /* upper sanity bound (e.g. RC_US_MAX) */
    uint8_t debounce_frames;   /* consecutive frames a level must hold */
} ch4_switch_cfg;

/**
 * Persistent debounce / edge-detection state for one CH4 switch. Carries no
 * timing of its own; updated in place every frame.
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
} ch4_switch_state;

/**
 * Initialise a CH4 switch state to "low, uninitialised".
 *
 * @param st  State to initialise (must be non-NULL).
 */
void ch4_switch_init(ch4_switch_state *st);

/**
 * Advance the CH4 position-switch interpreter by one frame.
 *
 * @param st   Persistent state, updated in place (must be non-NULL).
 * @param ch4  Latest raw CH4 capture sample (must be non-NULL).
 * @param cfg  Switch configuration (must be non-NULL).
 * @return CH4_SWITCH_TO_HIGH on the accepted low->high edge, CH4_SWITCH_TO_LOW
 *         on the accepted high->low edge, CH4_SWITCH_NONE otherwise (including
 *         the initial baseline frame, a held position, and any out-of-band /
 *         no-edge frame).
 */
ch4_switch_event ch4_switch_update(ch4_switch_state *st,
                                   const rc_channel_sample *ch4,
                                   const ch4_switch_cfg *cfg);

#ifdef __cplusplus
}
#endif
