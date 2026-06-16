#include "http_server.h"

#include <string.h>

#include "api_contract.h"
#include "cJSON.h"
#include "command_parse.h"
#include "control_loop.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "params_api.h"
#include "ws_telemetry.h"

static const char *TAG = "http_server";

/* Bound on the command POST body. A {"cmd":"..."} object is tens of bytes; this
 * leaves generous headroom while capping the read. */
#define HTTP_CMD_MAX 256

/* Bounded retry budget for transient recv timeouts while reading a body. */
#define HTTP_RECV_MAX_TIMEOUTS 4

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

/* Read the full request body into buf (NUL-terminated). Loops httpd_req_recv
 * until the declared content_len arrives, so a partial TCP read never silently
 * truncates the body. Returns -1 on overflow (body >= buf_size), connection
 * close before completion, a persistent timeout, or any recv error. */
static int read_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    size_t content_len = req->content_len;
    if (content_len >= buf_size) {
        return -1; /* too large for the contract buffer: reject, never truncate */
    }
    size_t received = 0;
    int timeouts = 0;
    while (received < content_len) {
        int n = httpd_req_recv(req, buf + received, content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts > HTTP_RECV_MAX_TIMEOUTS) {
                return -1;
            }
            continue;
        }
        if (n <= 0) {
            return -1; /* <0: recv error; 0: peer closed before full body */
        }
        received += (size_t)n;
    }
    buf[received] = '\0';
    return (int)received;
}

static esp_err_t post_params(httpd_req_t *req)
{
    char reqbuf[HTTP_REQ_MAX];
    char body[HTTP_BODY_MAX];
    if (read_body(req, reqbuf, sizeof(reqbuf)) < 0) {
        size_t len = api_build_error(API_ERR_BAD_REQUEST, NULL, body, sizeof(body));
        return send_api_response(req, 400, body, len);
    }
    params_api_response resp = params_api_handle_post(reqbuf, body, sizeof(body));
    return send_api_response(req, resp.http_status, body, resp.body_len);
}

/* Extract the "cmd" string from a {"cmd":"..."} body via cJSON (exact field).
 * Copies the keyword into out and returns true; false on parse/shape failure.
 * No substring matching: the keyword set is owned by the pure command_parse. */
static bool extract_command(const char *body, char *out, size_t out_size)
{
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return false;
    }
    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    bool ok = cJSON_IsString(cmd) && cmd->valuestring != NULL &&
              strlen(cmd->valuestring) < out_size;
    if (ok) {
        strcpy(out, cmd->valuestring);
    }
    cJSON_Delete(root);
    return ok;
}

/* Convert the pure parser's fields into the loop's UI event struct. */
static control_loop_ui_events to_ui_events(const command_parse_result *parsed)
{
    control_loop_ui_events ev = {
        .arm_request = parsed->arm_request,
        .disarm_request = parsed->disarm_request,
        .calib_request = parsed->calib_request,
        .calib_confirm = parsed->calib_confirm,
        .calib_event = parsed->calib_event,
    };
    return ev;
}

static esp_err_t send_command_error(httpd_req_t *req, api_error_code code)
{
    char body[HTTP_BODY_MAX];
    size_t len = api_build_error(code, NULL, body, sizeof(body));
    return send_api_response(req, 400, body, len);
}

static esp_err_t post_command(httpd_req_t *req)
{
    char reqbuf[HTTP_CMD_MAX];
    if (read_body(req, reqbuf, sizeof(reqbuf)) < 0) {
        return send_command_error(req, API_ERR_BAD_REQUEST);
    }
    char cmd[HTTP_CMD_MAX];
    if (!extract_command(reqbuf, cmd, sizeof(cmd))) {
        return send_command_error(req, API_ERR_BAD_REQUEST);
    }
    command_parse_result parsed = command_parse(cmd);
    if (!parsed.ok) {
        return send_command_error(req, API_ERR_VALIDATION_FAILED);
    }
    control_loop_ui_events ev = to_ui_events(&parsed);
    control_loop_post_ui_events(&ev);

    char body[HTTP_BODY_MAX];
    size_t len = api_build_success(NULL, body, sizeof(body));
    return send_api_response(req, 200, body, len);
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
