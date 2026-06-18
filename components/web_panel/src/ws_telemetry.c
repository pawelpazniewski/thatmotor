#include "ws_telemetry.h"

#include <stdatomic.h>
#include <stdio.h>

#include "control_loop.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ws_telemetry";

/* The single client slot + server handle. A push in flight is tracked with an
 * atomic flag so a slow client drops frames instead of backing up. */
static httpd_handle_t s_server;
static int s_client_fd = -1;
static esp_timer_handle_t s_timer;
static atomic_bool s_push_in_flight;

/* Serialise the snapshot to a compact JSON line. Hand-rolled (no cJSON alloc on
 * the hot path): all fields are small integers / booleans / an enum. */
static int snapshot_to_json(const control_loop_snapshot *s, char *buf, size_t n)
{
    return snprintf(buf, n,
        "{\"state\":%d,\"arm_reason\":%u,\"rc_valid\":%s,\"ch1_us\":%u,"
        "\"ch2_us\":%u,\"ch4_us\":%u,\"ch3_us\":%u,\"ch1_period_us\":%u,"
        "\"ch2_period_us\":%u,"
        "\"ch1_valid\":%s,\"ch2_valid\":%s,\"servo_us\":%u,\"esc_us\":%u,"
        "\"source\":%d,\"settings_valid\":%s,\"calibrated\":%s,"
        "\"defaults_used\":%s,\"nvs_error\":%s}",
        (int)s->state, (unsigned)s->arm_reason, s->rc_valid ? "true" : "false",
        (unsigned)s->ch1_us, (unsigned)s->ch2_us, (unsigned)s->ch4_us,
        (unsigned)s->ch3_us,
        (unsigned)s->ch1_period_us, (unsigned)s->ch2_period_us,
        s->ch1_valid ? "true" : "false", s->ch2_valid ? "true" : "false",
        (unsigned)s->servo_us, (unsigned)s->esc_us, (int)s->source,
        s->settings_valid ? "true" : "false",
        s->calibrated ? "true" : "false",
        s->defaults_used ? "true" : "false",
        s->nvs_error ? "true" : "false");
}

/* httpd work callback: runs in the server task. Sends the latest snapshot to
 * the client, then clears the in-flight flag so the next tick may push again. */
static void push_work(void *arg)
{
    (void)arg;
    if (s_client_fd < 0) {
        atomic_store(&s_push_in_flight, false);
        return;
    }
    control_loop_snapshot snap;
    control_loop_get_snapshot(&snap);

    char json[384];
    int len = snapshot_to_json(&snap, json, sizeof(json));
    if (len <= 0 || (size_t)len >= sizeof(json)) {
        atomic_store(&s_push_in_flight, false);
        return;
    }
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = (size_t)len,
    };
    esp_err_t err = httpd_ws_send_frame_async(s_server, s_client_fd, &frame);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ws send failed (0x%x), dropping client", err);
        s_client_fd = -1;
    }
    atomic_store(&s_push_in_flight, false);
}

/* Timer tick: queue a push unless one is still in flight (lossy drop). */
static void on_tick(void *arg)
{
    (void)arg;
    if (s_server == NULL || s_client_fd < 0) {
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
    s_client_fd = fd;
    atomic_store(&s_push_in_flight, false);
    return ensure_timer();
}

void ws_telemetry_stop(void)
{
    if (s_timer != NULL) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_client_fd = -1;
    s_server = NULL;
}
