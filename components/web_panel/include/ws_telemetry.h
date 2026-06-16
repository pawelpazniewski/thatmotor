#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * WebSocket telemetry push (Unit 10): lossy ~10 Hz snapshot producer.
 *
 * One WS client slot. A periodic timer reads the latest control_loop snapshot,
 * serialises it to JSON, and pushes it via httpd_queue_work. If a previous push
 * is still in flight (client behind), the new frame is DROPPED (lossy: telemetry
 * is always-latest, never queued/backpressured) so the control loop and server
 * are never blocked by a slow client.
 */

/* Telemetry push rate (~10 Hz, within the 5..20 Hz band). */
#define WS_TELEMETRY_PERIOD_MS 100

/**
 * Register a connected WS client as the single telemetry sink and start the
 * push timer (idempotent: a second client replaces the slot).
 *
 * @param server  The running httpd handle.
 * @param fd      The WS socket descriptor for the client.
 * @return ESP_OK on success, otherwise the failing esp_err_t.
 */
esp_err_t ws_telemetry_register(httpd_handle_t server, int fd);

/**
 * Stop the push timer and clear the client slot (e.g. on server teardown).
 */
void ws_telemetry_stop(void);

#ifdef __cplusplus
}
#endif
