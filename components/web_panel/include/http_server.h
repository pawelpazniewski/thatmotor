#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Embedded HTTP server (Unit 10): esp_http_server with URI handlers for the
 * panel assets, the params REST API, the UI command endpoint, and the telemetry
 * WebSocket.
 *
 * Routes:
 *   GET  /            -> index.html  (embedded)
 *   GET  /app.js      -> app.js      (embedded)
 *   GET  /style.css   -> style.css   (embedded)
 *   GET  /api/params  -> current active params  ({data,error} envelope)
 *   POST /api/params  -> validate + stage pending (409 if not DISARMED)
 *   POST /api/command -> arm/disarm/calibration UI events
 *   GET  /ws          -> telemetry WebSocket (~10 Hz, lossy)
 */

/**
 * Start the HTTP server and register all URI handlers. WS support must be
 * enabled in sdkconfig (CONFIG_HTTPD_WS_SUPPORT).
 *
 * @return ESP_OK on success, otherwise the failing esp_err_t.
 */
esp_err_t http_server_start(void);

#ifdef __cplusplus
}
#endif
