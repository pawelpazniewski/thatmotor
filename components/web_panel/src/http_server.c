#include "http_server.h"

#include <string.h>

#include "control_loop.h"
#include "esc_calibration.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "params_api.h"
#include "ws_telemetry.h"

static const char *TAG = "http_server";

/* Embedded panel assets (see EMBED_FILES in CMakeLists). */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");

/* Max request/response sizes. Params envelope fits comfortably under 1 KiB. */
#define HTTP_BODY_MAX 1024
#define HTTP_REQ_MAX 1024

static esp_err_t send_asset(httpd_req_t *req, const uint8_t *start,
                            const uint8_t *end, const char *content_type)
{
    httpd_resp_set_type(req, content_type);
    return httpd_resp_send(req, (const char *)start, end - start);
}

static esp_err_t get_index(httpd_req_t *req)
{
    return send_asset(req, index_html_start, index_html_end, "text/html");
}

static esp_err_t get_app_js(httpd_req_t *req)
{
    return send_asset(req, app_js_start, app_js_end, "application/javascript");
}

static esp_err_t get_style_css(httpd_req_t *req)
{
    return send_asset(req, style_css_start, style_css_end, "text/css");
}

/* Map a params_api HTTP status to its esp_http_server status line. */
static const char *status_line(int status)
{
    switch (status) {
    case 200:
        return "200 OK";
    case 400:
        return "400 Bad Request";
    case 409:
        return "409 Conflict";
    default:
        return "500 Internal Server Error";
    }
}

static esp_err_t send_api_response(httpd_req_t *req, int status,
                                   const char *body, size_t len)
{
    httpd_resp_set_status(req, status_line(status));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, len);
}

static esp_err_t get_params(httpd_req_t *req)
{
    char body[HTTP_BODY_MAX];
    params_api_response resp = params_api_handle_get(body, sizeof(body));
    return send_api_response(req, resp.http_status, body, resp.body_len);
}

/* Read the request body into buf (NUL-terminated). Returns -1 on overflow/error. */
static int read_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    if (req->content_len >= buf_size) {
        return -1;
    }
    int received = httpd_req_recv(req, buf, req->content_len);
    if (received < 0) {
        return -1;
    }
    buf[received] = '\0';
    return received;
}

static esp_err_t post_params(httpd_req_t *req)
{
    char reqbuf[HTTP_REQ_MAX];
    if (read_body(req, reqbuf, sizeof(reqbuf)) < 0) {
        char body[HTTP_BODY_MAX];
        params_api_response r = params_api_handle_post("", body, sizeof(body));
        return send_api_response(req, 400, body, r.body_len);
    }
    char body[HTTP_BODY_MAX];
    params_api_response resp = params_api_handle_post(reqbuf, body, sizeof(body));
    return send_api_response(req, resp.http_status, body, resp.body_len);
}

/* Translate a command keyword in the request body into a UI event set. The body
 * is a tiny JSON like {"cmd":"arm"}; a substring match keeps this dependency-free. */
static control_loop_ui_events parse_command(const char *body)
{
    control_loop_ui_events ev = {0};
    if (strstr(body, "\"arm\"") != NULL) {
        ev.arm_request = true;
    } else if (strstr(body, "\"disarm\"") != NULL) {
        ev.disarm_request = true;
    } else if (strstr(body, "\"calib_start\"") != NULL) {
        ev.calib_request = true;
        ev.calib_confirm = true;
    } else if (strstr(body, "\"calib_next\"") != NULL) {
        ev.calib_event = CALIB_EVENT_NEXT;
    } else if (strstr(body, "\"calib_cancel\"") != NULL) {
        ev.calib_event = CALIB_EVENT_CANCEL;
    }
    return ev;
}

static esp_err_t post_command(httpd_req_t *req)
{
    char reqbuf[256];
    if (read_body(req, reqbuf, sizeof(reqbuf)) < 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "{\"data\":null,\"error\":{\"code\":"
            "\"BAD_REQUEST\",\"message\":\"bad command\"}}", HTTPD_RESP_USE_STRLEN);
    }
    control_loop_ui_events ev = parse_command(reqbuf);
    control_loop_post_ui_events(&ev);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"data\":null,\"error\":null}",
                           HTTPD_RESP_USE_STRLEN);
}

/* WS handshake + frames. On the opening handshake, register this socket as the
 * single telemetry sink. Inbound frames are ignored (telemetry is push-only). */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "ws client connected (fd=%d)", fd);
        return ws_telemetry_register(req->handle, fd);
    }
    httpd_ws_frame_t frame = {.type = HTTPD_WS_TYPE_TEXT};
    return httpd_ws_recv_frame(req, &frame, 0); /* drain length, ignore payload */
}

static void register_handlers(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = get_index},
        {.uri = "/app.js", .method = HTTP_GET, .handler = get_app_js},
        {.uri = "/style.css", .method = HTTP_GET, .handler = get_style_css},
        {.uri = "/api/params", .method = HTTP_GET, .handler = get_params},
        {.uri = "/api/params", .method = HTTP_POST, .handler = post_params},
        {.uri = "/api/command", .method = HTTP_POST, .handler = post_command},
        {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler,
         .is_websocket = true},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    }
}

esp_err_t http_server_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        return err;
    }
    register_handlers(server);
    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}
