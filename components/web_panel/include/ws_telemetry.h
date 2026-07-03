#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * WebSocket telemetry push: lossy ~10 Hz snapshot producer, multi-client.
 *
 * Up to WS_TELEMETRY_MAX_CLIENTS clients (panel + app + spare) receive the same
 * telemetry in parallel. A periodic timer reads the latest control_loop snapshot,
 * serialises it to JSON ONCE, and broadcasts it to every client via
 * httpd_queue_work. If a previous push is still in flight (a client behind), the
 * new frame is DROPPED (lossy: telemetry is always-latest, never queued/
 * backpressured) so the control loop and server are never blocked by a slow
 * client. A client whose send fails is dropped from the list; the rest continue.
 */

/* Telemetry push rate (~10 Hz, within the 5..20 Hz band). */
#define WS_TELEMETRY_PERIOD_MS 100

/**
 * Add a connected WS client to the telemetry broadcast list and start the push
 * timer (idempotent: re-registering the same fd is a no-op). If the list is full
 * the newest client is rejected (logged) and telemetry continues for the rest.
 *
 * @param server  The running httpd handle.
 * @param fd      The WS socket descriptor for the client.
 * @return ESP_OK on success (including a full-list rejection), otherwise the
 *         failing esp_err_t from timer setup.
 */
esp_err_t ws_telemetry_register(httpd_handle_t server, int fd);

/**
 * Stop the push timer and clear the client list (e.g. on server teardown).
 */
void ws_telemetry_stop(void);

#ifdef __cplusplus
}
#endif
