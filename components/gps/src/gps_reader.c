#include "gps.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "gps";

/* UART wiring for the NEO-M9N (NMEA, 38400 8N1). UART1's default pins are the
 * flash pins (9/10), so we MUST remap with uart_set_pin. GPS TXD -> ESP RX,
 * GPS RXD <- ESP TX. */
#define GPS_UART_PORT UART_NUM_1
#define GPS_UART_BAUD 38400
#define GPS_UART_TX_GPIO 17
#define GPS_UART_RX_GPIO 16
#define GPS_UART_RX_BUF 1024

/* Reader task: low priority so it can never preempt or stall the 50 Hz control
 * loop. The GPS is diagnostic and entirely outside failsafe. */
#define GPS_TASK_STACK 3072
#define GPS_TASK_PRIO 2
#define GPS_READ_CHUNK 256
#define GPS_LINE_MAX 96
#define GPS_READ_TIMEOUT_MS 200

/* Shared decoded state, single-writer = the reader task, read via gps_get_state
 * under a short mutex. Outside failsafe: only feeds the telemetry snapshot. */
static gps_state s_state;
static SemaphoreHandle_t s_mutex;

/* Line accumulator carried across reads so a sentence split over two UART reads
 * is reassembled. Overlong lines (no terminator within GPS_LINE_MAX) are reset. */
static char s_line[GPS_LINE_MAX];
static size_t s_line_len;

/* Feed one received byte into the line accumulator; on a line terminator, parse
 * the completed line and merge any update into the shared state under mutex. */
static void feed_byte(char c)
{
    if (c == '\r' || c == '\n') {
        if (s_line_len == 0) {
            return;
        }
        s_line[s_line_len] = '\0';
        gps_state parsed;
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            parsed = s_state;
            if (nmea_parse_line(s_line, &parsed)) {
                s_state = parsed;
            }
            xSemaphoreGive(s_mutex);
        }
        s_line_len = 0;
        return;
    }
    if (s_line_len + 1 >= sizeof(s_line)) {
        s_line_len = 0; /* overlong garbage: drop and resync on next terminator */
        return;
    }
    s_line[s_line_len++] = c;
}

static void gps_task(void *arg)
{
    (void)arg;
    uint8_t buf[GPS_READ_CHUNK];
    for (;;) {
        int len = uart_read_bytes(GPS_UART_PORT, buf, sizeof(buf),
                                  pdMS_TO_TICKS(GPS_READ_TIMEOUT_MS));
        for (int i = 0; i < len; ++i) {
            feed_byte((char)buf[i]);
        }
    }
}

/* Configure and install the UART driver, then remap pins off the flash pins. */
static esp_err_t init_uart(void)
{
    const uart_config_t cfg = {
        .baud_rate = GPS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(GPS_UART_PORT, GPS_UART_RX_BUF, 0, 0,
                                        NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(GPS_UART_PORT, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    return uart_set_pin(GPS_UART_PORT, GPS_UART_TX_GPIO, GPS_UART_RX_GPIO,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

esp_err_t gps_start(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t err = init_uart();
    if (err != ESP_OK) {
        return err;
    }
    BaseType_t ok = xTaskCreate(gps_task, "gps", GPS_TASK_STACK, NULL,
                                GPS_TASK_PRIO, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "GPS reader up: UART%d RX=GPIO%d TX=GPIO%d @ %d baud",
             GPS_UART_PORT, GPS_UART_RX_GPIO, GPS_UART_TX_GPIO, GPS_UART_BAUD);
    return ESP_OK;
}

void gps_get_state(gps_state *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (s_mutex == NULL) {
        return;
    }
    /* Non-blocking: if the reader holds the mutex, skip this frame (out stays
     * zeroed) rather than stall the control task. */
    if (xSemaphoreTake(s_mutex, 0) == pdTRUE) {
        *out = s_state;
        xSemaphoreGive(s_mutex);
    }
}
