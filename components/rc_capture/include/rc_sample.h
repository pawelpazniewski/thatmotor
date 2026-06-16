#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Logical RC capture channels.
 *
 * RC_CAP_CH1 -> GPIO34 (steering, part of RC_valid)
 * RC_CAP_CH2 -> GPIO35 (throttle, part of RC_valid)
 * RC_CAP_CH4 -> GPIO32 (diagnostic only, deliberately outside RC_valid; R12)
 *
 * Framework-agnostic (no IDF includes) so the validity predicate and its host
 * tests can use these types without linking the MCPWM driver.
 */
typedef enum {
    RC_CAP_CH1 = 0,
    RC_CAP_CH2 = 1,
    RC_CAP_CH4 = 2,
    RC_CAP_CHANNEL_COUNT
} RcCaptureChannel;

/**
 * Raw, hardware-timestamped sample for a single RC channel.
 *
 * Produced by the MCPWM capture callback and consumed (without modification)
 * by the validity predicate in rc_validity. Carries no validity judgement of
 * its own.
 *
 * width_us     Last measured pulse width (high time) in microseconds.
 * period_us    Last measured frame period (rising-edge to rising-edge) in us.
 * last_edge_us Timestamp of the last rising edge, in microseconds, for
 *              edge-recency checks. Monotonic within a counter epoch.
 * edge_seen    True once at least one complete pulse has been measured.
 */
typedef struct {
    uint32_t width_us;
    uint32_t period_us;
    uint32_t last_edge_us;
    bool edge_seen;
} rc_channel_sample;

#ifdef __cplusplus
}
#endif
