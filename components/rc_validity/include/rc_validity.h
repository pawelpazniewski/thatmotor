#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rc_sample.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Validity thresholds for a single RC channel. Framework-agnostic config so
 * the predicate stays pure and host-testable.
 *
 * The expected frame period is NOT hardcoded to 20 ms / 50 Hz: it is supplied
 * here from the measured receiver rate (kontekst: "NIE zakładać 20 ms"), with a
 * generous tolerance band. A pulse is valid only when its width is in
 * [width_min_us, width_max_us], its frame period is within tolerance of
 * expected, and an edge was seen within edge_timeout_us.
 */
typedef struct {
    uint32_t width_min_us;       /* min accepted pulse width */
    uint32_t width_max_us;       /* max accepted pulse width */
    uint32_t period_expected_us; /* measured frame period */
    uint32_t period_tol_us;      /* +/- tolerance around expected period */
    uint32_t edge_timeout_us;    /* max age of the last edge to still count */
} rc_channel_cfg;

/**
 * Debounce state for an RC validity decision. The latched validity only flips
 * to invalid after invalid_threshold consecutive invalid frames, and resets
 * its bad-frame counter on the first valid frame.
 */
typedef struct {
    uint16_t bad_frame_count;  /* consecutive invalid frames seen */
    uint16_t invalid_threshold; /* N: bad frames required to latch invalid */
    bool latched_valid;        /* current debounced validity */
} rc_debounce_state;

/**
 * Default conservative consecutive-bad-frame threshold for failsafe.
 *
 * Chosen at the low end of the plan's "~5-10" range so a single glitch never
 * trips failsafe, but signal loss is caught within a few frames. Tuned after
 * the receiver frame-rate measurement.
 */
#define RC_DEBOUNCE_DEFAULT_THRESHOLD 5U

/**
 * Pure single-frame validity predicate for one channel.
 *
 * CONTRACT (epoch): now_ticks and sample->last_edge_ticks MUST both be in the
 * same MCPWM free-running 32-bit capture-counter domain (12.5 ns/tick). Recency
 * is computed with wrap-safe modular subtraction in that tick domain (via
 * cap_ticks_elapsed), so it stays correct right after boot and across every
 * ~53.6 s counter wrap. Do NOT pass esp_timer_get_time(): that is a different
 * epoch and does not wrap at 2^32 ticks. The producer of now_ticks is the same
 * MCPWM capture timer that stamps last_edge_ticks.
 *
 * @param sample     Latest raw capture sample for the channel.
 * @param now_ticks  Current capture-counter tick (same domain as
 *                   sample->last_edge_ticks).
 * @param cfg        Validity thresholds (edge_timeout_us in microseconds).
 * @return true if this frame's pulse is valid, false otherwise.
 */
bool channel_valid(const rc_channel_sample *sample, uint32_t now_ticks,
                   const rc_channel_cfg *cfg);

/**
 * RC_valid := channel_valid(CH1) AND channel_valid(CH2). CH4 is deliberately
 * excluded (R12). Single-frame, no debounce.
 *
 * @return true only when both control channels are valid this frame.
 */
bool rc_valid(bool ch1_valid, bool ch2_valid);

/**
 * Initialise a debounce state: starts latched-valid with a clean counter.
 *
 * @param state      State to initialise (must be non-NULL).
 * @param threshold  Consecutive-bad-frame count required to latch invalid.
 */
void rc_debounce_init(rc_debounce_state *state, uint16_t threshold);

/**
 * Feed one frame's raw validity into the debounce filter and return the
 * debounced validity. A valid frame resets the bad-frame counter and latches
 * valid immediately; invalid frames accumulate and only latch invalid once the
 * threshold of consecutive invalid frames is reached.
 *
 * @param state        Debounce state (must be non-NULL).
 * @param frame_valid  Raw validity of the current frame.
 * @return The debounced validity after this frame.
 */
bool rc_debounce_update(rc_debounce_state *state, bool frame_valid);

#ifdef __cplusplus
}
#endif
