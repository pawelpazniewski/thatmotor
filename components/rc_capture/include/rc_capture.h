#pragma once

#include "esp_err.h"
#include "rc_sample.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the MCPWM capture timer and the three RC capture channels
 * (CH1/CH2/CH4) with their rising/falling-edge callback.
 *
 * Fail-fast: returns the first underlying esp_err_t on failure. On success the
 * capture timer is running and samples begin updating asynchronously.
 *
 * @return ESP_OK on success, otherwise the failing esp_err_t.
 */
esp_err_t rc_capture_init(void);

/**
 * Copy the latest sample for a channel into *out.
 *
 * The copy is a plain struct read; the callback may update the backing sample
 * concurrently, so callers should treat the result as a best-effort snapshot.
 *
 * @param channel  Channel to read.
 * @param out      Destination sample (must be non-NULL).
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for a bad channel or NULL out.
 */
esp_err_t rc_capture_read(RcCaptureChannel channel, rc_channel_sample *out);

/**
 * Current recency tick in the rc_capture domain (12.5 ns/tick, 32-bit, wraps at
 * 2^32 ~ 53.6 s). This is the value to pass as channel_valid's now_ticks: it is
 * the same domain as rc_channel_sample.last_edge_ticks, advances in real time
 * (so edge recency expires even when RC stops), and wraps identically. Derived
 * from esp_timer, NOT the raw hardware capture counter.
 *
 * @return Current tick in the rc_capture recency domain.
 */
uint32_t rc_capture_now_ticks(void);

#ifdef __cplusplus
}
#endif
