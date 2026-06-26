#include "imu.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "quat_to_yaw.h"

static const char *TAG = "imu";

/* --- Wiring (GY-BN008X, ADO->GND so I2C address is 0x4A, PS0/PS1->GND for the
 * I2C protocol). Power is 3.3 V. RST is an active-low reset; INT is left
 * unused here (we poll the data register instead of waiting on the line). --- */
#define IMU_I2C_PORT I2C_NUM_0
#define IMU_I2C_ADDR 0x4A
/* ESP32-S3 N16R8 pin map: SDA stays on GPIO21; SCL/INT/RST move off the
 * non-existent S3 pins 22/23/25 onto free I/O pins. */
#define IMU_I2C_SDA_GPIO 21
#define IMU_I2C_SCL_GPIO 47
#define IMU_I2C_INT_GPIO 14
#define IMU_I2C_RST_GPIO 13
#define IMU_I2C_FREQ_HZ 100000 /* 100 kHz: safe for the long hobby wiring */
#define IMU_I2C_TIMEOUT_MS 50

/* --- SHTP (Sensor Hub Transport Protocol) framing. Every cargo is a 4-byte
 * header: length LSB, length MSB (with bit15 = "continuation", masked off),
 * channel, sequence; followed by the payload. Length INCLUDES the 4 header
 * bytes. --- */
#define SHTP_HEADER_LEN 4
#define SHTP_LEN_CONTINUATION_MASK 0x8000
#define SHTP_CHANNEL_CONTROL 2 /* host -> hub commands (set-feature) */
#define SHTP_CHANNEL_REPORTS 3 /* hub -> host normalized sensor reports */

/* --- SH-2 sensor reports. We enable the north-referenced Rotation Vector
 * (report id 0x05) via a Set Feature Command (report id 0xFD). Game Rotation
 * Vector (0x08) is NOT used: it has no magnetometer, so no real heading. --- */
#define SH2_REPORT_ROTATION_VECTOR 0x05
#define SH2_CMD_SET_FEATURE 0xFD
#define SH2_REPORT_INTERVAL_US 50000 /* 50 ms -> 20 Hz */

/* Rotation Vector input report layout (after the SHTP header), Q-point per the
 * SH-2 reference manual: i/j/k/real are Q14, accuracy byte is the status. */
#define RV_OFFS_STATUS 7      /* low 2 bits = accuracy (0..3) */
#define RV_OFFS_QI_LSB 9
#define RV_OFFS_QJ_LSB 11
#define RV_OFFS_QK_LSB 13
#define RV_OFFS_QREAL_LSB 15
#define RV_MIN_PAYLOAD 17     /* bytes after the 4-byte header to hold q_real */
#define RV_ACCURACY_MASK 0x03

/* Reader task: low priority so it can never preempt or stall the 50 Hz control
 * loop. The IMU is diagnostic and entirely outside failsafe. */
#define IMU_TASK_STACK 4096
#define IMU_TASK_PRIO 2
#define IMU_READ_BUF 128
#define IMU_POLL_PERIOD_MS 50
#define IMU_STALE_AFTER_MS 1000 /* no fresh report within this -> ok=false */

#define MS_PER_SEC 1000

/* Shared decoded state, single-writer = the reader task, read via imu_get_state
 * under a short mutex. Outside failsafe: only feeds the telemetry snapshot. */
static imu_state s_state;
static SemaphoreHandle_t s_mutex;
static uint8_t s_seq_control; /* outgoing sequence number on the control channel */

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static esp_err_t i2c_write(const uint8_t *data, size_t len)
{
    return i2c_master_write_to_device(IMU_I2C_PORT, IMU_I2C_ADDR, data, len,
                                      pdMS_TO_TICKS(IMU_I2C_TIMEOUT_MS));
}

static esp_err_t i2c_read(uint8_t *data, size_t len)
{
    return i2c_master_read_from_device(IMU_I2C_PORT, IMU_I2C_ADDR, data, len,
                                       pdMS_TO_TICKS(IMU_I2C_TIMEOUT_MS));
}

/* Build and send the SHTP cargo for an SH-2 Set Feature Command enabling the
 * north-referenced Rotation Vector at SH2_REPORT_INTERVAL_US. */
static esp_err_t enable_rotation_vector(void)
{
    uint8_t cargo[SHTP_HEADER_LEN + 17];
    memset(cargo, 0, sizeof(cargo));
    uint16_t total = sizeof(cargo);
    cargo[0] = (uint8_t)(total & 0xFF);
    cargo[1] = (uint8_t)((total >> 8) & 0xFF);
    cargo[2] = SHTP_CHANNEL_CONTROL;
    cargo[3] = s_seq_control++;

    uint8_t *body = &cargo[SHTP_HEADER_LEN];
    body[0] = SH2_CMD_SET_FEATURE;
    body[1] = SH2_REPORT_ROTATION_VECTOR;
    /* body[2..4] feature flags / sensitivity = 0; body[5..8] report interval. */
    body[5] = (uint8_t)(SH2_REPORT_INTERVAL_US & 0xFF);
    body[6] = (uint8_t)((SH2_REPORT_INTERVAL_US >> 8) & 0xFF);
    body[7] = (uint8_t)((SH2_REPORT_INTERVAL_US >> 16) & 0xFF);
    body[8] = (uint8_t)((SH2_REPORT_INTERVAL_US >> 24) & 0xFF);

    return i2c_write(cargo, sizeof(cargo));
}

static int16_t le16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Decode a Rotation Vector input report payload (one SHTP cargo body, i.e. the
 * bytes AFTER the 4-byte SHTP header) into heading + calibration. Returns true
 * on a well-formed RV report. PURE-ish: no I2C, only quat_to_yaw_deg10. */
static bool decode_rotation_vector(const uint8_t *body, size_t body_len,
                                   uint16_t *heading_deg10, uint8_t *calib)
{
    if (body_len < RV_MIN_PAYLOAD || body[0] != SH2_REPORT_ROTATION_VECTOR) {
        return false;
    }
    int16_t qi = le16(&body[RV_OFFS_QI_LSB]);
    int16_t qj = le16(&body[RV_OFFS_QJ_LSB]);
    int16_t qk = le16(&body[RV_OFFS_QK_LSB]);
    int16_t qreal = le16(&body[RV_OFFS_QREAL_LSB]);
    *heading_deg10 = quat_to_yaw_deg10(qi, qj, qk, qreal);
    *calib = body[RV_OFFS_STATUS] & RV_ACCURACY_MASK;
    return true;
}

/* Read one SHTP cargo. Returns the channel and the payload-after-header bytes
 * via out params; returns the payload length, or 0 if nothing/garbage. */
static size_t read_cargo(uint8_t *buf, size_t cap, uint8_t *channel)
{
    uint8_t header[SHTP_HEADER_LEN];
    if (i2c_read(header, sizeof(header)) != ESP_OK) {
        return 0;
    }
    uint16_t len = (uint16_t)header[0] | ((uint16_t)header[1] << 8);
    len &= ~SHTP_LEN_CONTINUATION_MASK;
    if (len <= SHTP_HEADER_LEN) {
        return 0; /* empty cargo (header only) or zero length */
    }
    *channel = header[2];
    size_t body_len = len - SHTP_HEADER_LEN;
    if (body_len > cap) {
        body_len = cap; /* clamp; we only need the first RV_MIN_PAYLOAD bytes */
    }
    /* Re-read the full cargo (header + body) and hand back just the body. */
    uint8_t frame[SHTP_HEADER_LEN + IMU_READ_BUF];
    size_t total = SHTP_HEADER_LEN + body_len;
    if (total > sizeof(frame) || i2c_read(frame, total) != ESP_OK) {
        return 0;
    }
    memcpy(buf, &frame[SHTP_HEADER_LEN], body_len);
    return body_len;
}

static void store_state(bool ok, uint16_t heading_deg10, uint8_t calib)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_state.ok = ok;
    if (ok) {
        s_state.heading_deg10 = heading_deg10;
        s_state.calib = calib;
    }
    xSemaphoreGive(s_mutex);
}

static void imu_task(void *arg)
{
    (void)arg;
    uint8_t body[IMU_READ_BUF];
    uint32_t last_report_ms = now_ms();
    for (;;) {
        uint8_t channel = 0;
        size_t body_len = read_cargo(body, sizeof(body), &channel);
        if (body_len > 0 && channel == SHTP_CHANNEL_REPORTS) {
            uint16_t heading;
            uint8_t calib;
            if (decode_rotation_vector(body, body_len, &heading, &calib)) {
                store_state(true, heading, calib);
                last_report_ms = now_ms();
            }
        }
        if (now_ms() - last_report_ms > IMU_STALE_AFTER_MS) {
            store_state(false, 0, 0); /* stale: panel flag only, NOT failsafe */
        }
        vTaskDelay(pdMS_TO_TICKS(IMU_POLL_PERIOD_MS));
    }
}

static esp_err_t init_i2c(void)
{
    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = IMU_I2C_SDA_GPIO,
        .scl_io_num = IMU_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = IMU_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(IMU_I2C_PORT, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    return i2c_driver_install(IMU_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
}

/* Drive RST low then high to reset the BNO085, leaving INT as an input. */
static esp_err_t reset_sensor(void)
{
    const gpio_config_t out = {
        .pin_bit_mask = 1ULL << IMU_I2C_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    const gpio_config_t in = {
        .pin_bit_mask = 1ULL << IMU_I2C_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&out);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_config(&in);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(IMU_I2C_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(IMU_I2C_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(200)); /* let the hub boot + emit advertisement */
    return ESP_OK;
}

esp_err_t imu_start(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t err = init_i2c();
    if (err != ESP_OK) {
        return err;
    }
    err = reset_sensor();
    if (err != ESP_OK) {
        return err;
    }
    err = enable_rotation_vector();
    if (err != ESP_OK) {
        return err;
    }
    BaseType_t ok = xTaskCreate(imu_task, "imu", IMU_TASK_STACK, NULL,
                                IMU_TASK_PRIO, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "IMU up: BNO085 I2C%d addr=0x%02X SDA=%d SCL=%d RST=%d",
             IMU_I2C_PORT, IMU_I2C_ADDR, IMU_I2C_SDA_GPIO, IMU_I2C_SCL_GPIO,
             IMU_I2C_RST_GPIO);
    return ESP_OK;
}

void imu_get_state(imu_state *out)
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
