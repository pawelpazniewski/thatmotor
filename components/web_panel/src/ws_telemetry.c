#include "ws_telemetry.h"

#include <stdatomic.h>
#include <stdio.h>

#include "control_loop.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ws_client_set.h"
#include "telemetry_json.h"

static const char *TAG = "ws_telemetry";

/*
 * Threading invariant (CRITICAL — do not break):
 *   The client list (s_clients) is mutated ONLY on the httpd task:
 *     - ws_telemetry_register (called from the httpd ws_handler)
 *     - push_work            (dispatched via httpd_queue_work -> httpd task)
 *   on_tick runs on the esp_timer task and MUST NOT touch the list. It only
 *   reads s_server, flips the atomic in-flight flag, and queues push_work.
 *   Because every list mutation is serialised on the single httpd task, the set
 *   needs no mutex. Adding a list mutation to on_tick (or any other task) would
 *   introduce a data race and require locking — don't.
 *
 * A push in flight is tracked with an atomic flag so a slow client drops frames
 * instead of backing up. */
static httpd_handle_t s_server;
static ws_client_set s_clients;
static bool s_clients_ready;
static esp_timer_handle_t s_timer;
static atomic_bool s_push_in_flight;

/* Lazily initialise the client set to empty. Runs on the httpd task (register),
 * so it respects the mutation invariant. Static zero-init would leave fds at 0
 * (a valid-looking fd), so an explicit init is required. */
static void ensure_clients_init(void)
{
    if (!s_clients_ready) {
        ws_client_set_init(&s_clients);
        s_clients_ready = true;
    }
}

/* httpd work callback: runs in the server task. Serialises the latest snapshot
 * ONCE and broadcasts it to every client, garbage-collecting any fd whose send
 * fails. Clears the in-flight flag so the next tick may push again. */
static void push_work(void *arg)
{
    (void)arg;
    size_t count = ws_client_set_count(&s_clients);
    if (count == 0) {
        atomic_store(&s_push_in_flight, false);
        return;
    }
    control_loop_snapshot snap;
    control_loop_get_snapshot(&snap);

    char json[WS_TELEMETRY_JSON_MAX];
    int len = telemetry_json_format(&snap, json, sizeof(json));
    if (len <= 0 || (size_t)len >= sizeof(json)) {
        atomic_store(&s_push_in_flight, false);
        return;
    }
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = (size_t)len,
    };
    /* Collect failed fds and remove them AFTER the loop so we never mutate the
     * set while iterating it by index. */
    int failed[WS_TELEMETRY_MAX_CLIENTS];
    size_t failed_count = 0;
    for (size_t i = 0; i < count; ++i) {
        int fd = ws_client_set_at(&s_clients, i);
        esp_err_t err = httpd_ws_send_frame_async(s_server, fd, &frame);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ws send failed (0x%x), dropping client (fd=%d)", err, fd);
            failed[failed_count++] = fd;
        }
    }
    for (size_t j = 0; j < failed_count; ++j) {
        ws_client_set_remove(&s_clients, failed[j]);
    }
    atomic_store(&s_push_in_flight, false);
}

/* Timer tick: queue a push unless one is still in flight (lossy drop). */
static void on_tick(void *arg)
{
    (void)arg;
    if (s_server == NULL) {
        return;
    }
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_push_in_flight, &expected, true)) {
        return; /* previous push still in flight -> drop this frame */
    }
    if (httpd_queue_work(s_server, push_work, NULL) != ESP_OK) {
        atomic_store(&s_push_in_flight, false);
    }
}

static esp_err_t ensure_timer(void)
{
    if (s_timer != NULL) {
        return ESP_OK;
    }
    const esp_timer_create_args_t args = {
        .callback = on_tick,
        .name = "ws_tele",
    };
    esp_err_t err = esp_timer_create(&args, &s_timer);
    if (err != ESP_OK) {
        return err;
    }
    return esp_timer_start_periodic(s_timer, WS_TELEMETRY_PERIOD_MS * 1000);
}

esp_err_t ws_telemetry_register(httpd_handle_t server, int fd)
{
    s_server = server;
    ensure_clients_init();
    ws_client_add_result res = ws_client_set_add(&s_clients, fd);
    if (res == WS_CLIENT_FULL) {
        /* List full: reject the newest client (log-and-continue). The WS
         * handshake still succeeded; this client simply gets no telemetry. */
        ESP_LOGW(TAG, "telemetry client list full (max=%d), rejecting fd=%d",
                 WS_TELEMETRY_MAX_CLIENTS, fd);
        return ESP_OK;
    }
    return ensure_timer();
}

void ws_telemetry_stop(void)
{
    if (s_timer != NULL) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }
    ws_client_set_init(&s_clients);
    s_clients_ready = true;
    s_server = NULL;
}
