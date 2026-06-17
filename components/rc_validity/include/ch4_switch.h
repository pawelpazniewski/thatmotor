#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rc_sample.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure CH4-as-mode-button interpreter (framework-agnostic, no IDF includes).
 *
 * CH4 (GPIO32) is read by rc_capture as a normal RC channel but here it is
 * interpreted as a momentary push button: each rising edge of the "pressed"
 * level (released -> pressed) emits exactly ONE one-shot toggle pulse. The
 * caller routes that pulse into sm_inputs.mode_toggle (ARMED <-> DISARMED).
 *
 * Determinism / timing: this module owns NO clock. It works purely per frame
 * (one ch4_switch_update call per control cycle). Debounce is expressed in
 * frames, not milliseconds, so it is reproducible in host tests with no time
 * source and immune to tick-counter wrap.
 *
 * Semantics:
 *  - "pressed"  := the CH4 pulse is inside the sanity band AND its width is
 *                  >= threshold_us. Out-of-band / no edge seen -> "released".
 *  - debounce   := a candidate level must persist debounce_frames consecutive
 *                  frames before it is accepted as the new stable level.
 *  - edge       := a toggle pulse (true) is emitted ONLY when the accepted
 *                  stable level transitions released -> pressed. Holding the
 *                  button down emits nothing further; releasing emits nothing.
 *  - init       := the FIRST accepted stable level only establishes a baseline
 *                  (no pulse), so powering up with the button already held does
 *                  NOT arm.
 */

/* Consecutive control frames a CH4 level must hold before it is accepted. At
 * the ~50 Hz control rate, 3 frames ~= 60 ms: long enough to reject a single
 * glitch frame, short enough to feel instant. Lives here (not in settings) so
 * both the pure module and its HAL caller share one definition without crossing
 * the settings component's private boundary. */
#define CH4_SWITCH_DEBOUNCE_FRAMES 3U

/** Configuration for the CH4 button interpretation (all constant per build). */
typedef struct {
    uint16_t threshold_us;     /* width_us >= this (and in band) -> pressed */
    uint16_t sanity_min_us;    /* lower sanity bound (e.g. RC_US_MIN) */
    uint16_t sanity_max_us;    /* upper sanity bound (e.g. RC_US_MAX) */
    uint8_t debounce_frames;   /* consecutive frames a level must hold */
} ch4_switch_cfg;

/**
 * Persistent debounce / edge-detection state for one CH4 button. Carries no
 * timing of its own; updated in place every frame.
 *
 * pressed       Current accepted stable level (true = button down).
 * candidate     The level seen this frame that is being counted toward accept.
 * stable_frames Consecutive frames the candidate has matched.
 * initialised   True once a baseline level has been established (suppresses the
 *               very first pulse).
 */
typedef struct {
    bool pressed;
    bool candidate;
    uint8_t stable_frames;
    bool initialised;
} ch4_switch_state;

/**
 * Initialise a CH4 button state to "released, uninitialised".
 *
 * @param st  State to initialise (must be non-NULL).
 */
void ch4_switch_init(ch4_switch_state *st);

/**
 * Advance the CH4 button interpreter by one frame.
 *
 * @param st   Persistent state, updated in place (must be non-NULL).
 * @param ch4  Latest raw CH4 capture sample (must be non-NULL).
 * @param cfg  Button configuration (must be non-NULL).
 * @return true EXACTLY on the accepted released->pressed edge (one-shot toggle
 *         pulse); false otherwise (including the initial baseline frame and any
 *         out-of-band / no-edge frame).
 */
bool ch4_switch_update(ch4_switch_state *st, const rc_channel_sample *ch4,
                       const ch4_switch_cfg *cfg);

#ifdef __cplusplus
}
#endif
