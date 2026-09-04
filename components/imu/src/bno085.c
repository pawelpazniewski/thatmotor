#include "imu.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "quat_to_yaw.h"
#include "sh2.h"
#include "sh2_SensorValue.h"
#include "sh2_err.h"

static const char *TAG = "imu";

/* --- Wiring (BNO085, ADO->GND => I2C address 0x4A, PS0/PS1->GND => I2C mode).
 * Power 3.3 V. RST is active-low. INT (data-ready) is wired but not required:
 * this driver uses the CEVA SH-2 library with a polling I2C HAL. --- */
#define IMU_I2C_PORT I2C_NUM_0
#define IMU_I2C_ADDR 0x4A
#define IMU_I2C_SDA_GPIO 4
#define IMU_I2C_SCL_GPIO 5
#define IMU_I2C_RST_GPIO 7
#define IMU_I2C_FREQ_HZ 100000 /* 100 kHz: BNO085 clock-stretches; keep it slow */
#define IMU_I2C_TIMEOUT_MS 150 /* tolerate clock-stretch while the hub is busy */

/* SH-2 Rotation Vector (north-referenced, fuses the magnetometer => heading).
 * 50 Hz is ample for steering telemetry and keeps the I2C bus quiet. */
#define IMU_RV_INTERVAL_US 20000

/* Quaternion components arrive as floats in [-1, 1]; quat_to_yaw_deg10 (the
 * host-tested pure conversion) takes Q14 fixed point, so scale by 2^14. */
#define IMU_Q14_SCALE 16384.0f
#define IMU_ACCURACY_MASK 0x03

/* Reader task: low priority so it can never preempt or stall the 50 Hz control
 * loop. The IMU is diagnostic and entirely outside failsafe. */
#define IMU_TASK_STACK 4096
#define IMU_TASK_PRIO 2
#define IMU_SERVICE_PERIOD_MS 10
#define IMU_STALE_AFTER_MS 1000 /* no fresh report within this -> ok=false */

/* Shared decoded state, single-writer = the reader task, read via imu_get_state
 * under a short mutex. Outside failsafe: only feeds the telemetry snapshot. */
static imu_state s_state;
static SemaphoreHandle_t s_mutex;
static sh2_Hal_t s_hal;
static uint32_t s_last_report_ms;
static volatile bool s_need_reconfig; /* set by the SH-2 reset event callback */

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static esp_err_t i2c_write_raw(const uint8_t *data, size_t len)
{
    return i2c_master_write_to_device(IMU_I2C_PORT, IMU_I2C_ADDR, data, len,
                                      pdMS_TO_TICKS(IMU_I2C_TIMEOUT_MS));
}

static esp_err_t i2c_read_raw(uint8_t *data, size_t len)
{
    return i2c_master_read_from_device(IMU_I2C_PORT, IMU_I2C_ADDR, data, len,
                                       pdMS_TO_TICKS(IMU_I2C_TIMEOUT_MS));
}

/* --- SH-2 HAL (polling I2C, mirrors the Adafruit i2chal_* reference). --- */

/* Soft-reset the hub (SHTP executable channel, payload 0x01), retrying while it
 * is still booting, then let it settle before the SH-2 handshake starts. */
static int hal_open(sh2_Hal_t *self)
{
    (void)self;
    const uint8_t soft_reset[] = {5, 0, 1, 0, 1};
    for (int attempt = 0; attempt < 10; attempt++) {
        if (i2c_write_raw(soft_reset, sizeof(soft_reset)) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(300));
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    return -1;
}

static void hal_close(sh2_Hal_t *self)
{
    (void)self;
}

/* Read one SHTP cargo: the 4-byte header gives the length, then we read the
 * whole packet in one transaction (the hub re-presents header+body from the
 * start each read and only advances once the full length is consumed). */
static int hal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len,
                    uint32_t *t_us)
{
    (void)self;
    *t_us = (uint32_t)esp_timer_get_time();

    uint8_t header[4];
    if (i2c_read_raw(header, sizeof(header)) != ESP_OK) {
        return 0;
    }
    uint16_t packet_size =
        ((uint16_t)header[0] | ((uint16_t)header[1] << 8)) & ~0x8000U;
    if (packet_size == 0 || packet_size > len) {
        return 0;
    }
    if (i2c_read_raw(pBuffer, packet_size) != ESP_OK) {
        return 0;
    }
    return (int)packet_size;
}

static int hal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len)
{
    (void)self;
    unsigned n = len > SH2_HAL_MAX_TRANSFER_OUT ? SH2_HAL_MAX_TRANSFER_OUT : len;
    if (i2c_write_raw(pBuffer, n) != ESP_OK) {
        return 0;
    }
    return (int)n;
}

static uint32_t hal_get_time_us(sh2_Hal_t *self)
{
    (void)self;
    return (uint32_t)esp_timer_get_time();
}

static void store_state(bool ok, uint16_t heading_deg10, uint16_t raw_yaw_deg10,
                        uint8_t calib)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_state.ok = ok;
    if (ok) {
        s_state.heading_deg10 = heading_deg10;
        s_state.raw_yaw_deg10 = raw_yaw_deg10;
        s_state.calib = calib;
    }
    xSemaphoreGive(s_mutex);
}

/* SH-2 sensor report callback: decode the Rotation Vector quaternion into a
 * heading. Runs inside sh2_service() on the reader task. */
static void sensor_cb(void *cookie, sh2_SensorEvent_t *event)
{
    (void)cookie;
    sh2_SensorValue_t value;
    if (sh2_decodeSensorEvent(&value, event) != SH2_OK) {
        return;
    }
    if (value.sensorId != SH2_ROTATION_VECTOR) {
        return;
    }
    int16_t qi = (int16_t)lroundf(value.un.rotationVector.i * IMU_Q14_SCALE);
    int16_t qj = (int16_t)lroundf(value.un.rotationVector.j * IMU_Q14_SCALE);
    int16_t qk = (int16_t)lroundf(value.un.rotationVector.k * IMU_Q14_SCALE);
    int16_t qr = (int16_t)lroundf(value.un.rotationVector.real * IMU_Q14_SCALE);
    /* Diagnostic only, throttled to ~1 Hz (CONFIG_LOG_MAXIMUM_LEVEL is INFO,
     * so ESP_LOGD is compiled out): with the board lying flat, i and j near 0
     * confirm the mount's Z axis is truly up (a pure Z-axis rotation
     * quaternion has zero i/j) -- see
     * docs/dev-brainstorms/2026-09-04-imu-mount-offset-requirements.md R1/R2. */
    static int s_quat_log_divider;
    if (++s_quat_log_divider >= 50) {
        s_quat_log_divider = 0;
        ESP_LOGI(TAG, "raw quat (Q14) i=%d j=%d k=%d w=%d", qi, qj, qk, qr);
    }
    uint16_t raw_yaw = quat_to_yaw_deg10(qi, qj, qk, qr);
    uint16_t heading = yaw_to_compass_heading_deg10(raw_yaw);
    store_state(true, heading, raw_yaw, value.status & IMU_ACCURACY_MASK);
    s_last_report_ms = now_ms();
}

static esp_err_t enable_rotation_vector(void)
{
    sh2_SensorConfig_t config;
    memset(&config, 0, sizeof(config));
    config.reportInterval_us = IMU_RV_INTERVAL_US;
    return sh2_setSensorConfig(SH2_ROTATION_VECTOR, &config) == SH2_OK
               ? ESP_OK
               : ESP_FAIL;
}

/* Enable the SH-2 Motion Engine's own dynamic calibration for all three
 * fused sensors so the Rotation Vector's accuracy (imu_calib) can actually
 * climb above 0 -- without this call the hub never runs the on-chip
 * calibrator, no matter how much the boat is rotated by hand. */
static esp_err_t enable_calibration(void)
{
    return sh2_setCalConfig(SH2_CAL_ACCEL | SH2_CAL_GYRO | SH2_CAL_MAG) == SH2_OK
               ? ESP_OK
               : ESP_FAIL;
}

/* SH-2 async event callback: on a sensor reset the report/cal config is
 * lost, so flag the reader task to re-enable it (must NOT call sh2_*
 * re-entrantly). */
static void event_cb(void *cookie, sh2_AsyncEvent_t *event)
{
    (void)cookie;
    if (event->eventId == SH2_RESET) {
        s_need_reconfig = true;
    }
}

static void imu_task(void *arg)
{
    (void)arg;
    s_last_report_ms = now_ms();
    for (;;) {
        if (s_need_reconfig) {
            s_need_reconfig = false;
            enable_rotation_vector();
            enable_calibration();
        }
        sh2_service();
        if (now_ms() - s_last_report_ms > IMU_STALE_AFTER_MS) {
            store_state(false, 0, 0, 0); /* stale: panel flag only, NOT failsafe */
        }
        vTaskDelay(pdMS_TO_TICKS(IMU_SERVICE_PERIOD_MS));
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

/* Hold RST deasserted (high) and let the hub settle. We do NOT pulse RST: on
 * this board the pulse leaves the hub busy long enough that the first I2C
 * exchanges fail; the SH-2 soft reset in hal_open brings it to a known state. */
static esp_err_t reset_sensor(void)
{
    const gpio_config_t out = {
        .pin_bit_mask = 1ULL << IMU_I2C_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&out);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(IMU_I2C_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
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

    s_hal.open = hal_open;
    s_hal.close = hal_close;
    s_hal.read = hal_read;
    s_hal.write = hal_write;
    s_hal.getTimeUs = hal_get_time_us;

    int rc = sh2_open(&s_hal, event_cb, NULL);
    if (rc != SH2_OK) {
        ESP_LOGE(TAG, "sh2_open failed: %d", rc);
        return ESP_FAIL;
    }
    sh2_setSensorCallback(sensor_cb, NULL);

    err = enable_calibration();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enable calibration failed");
        return err;
    }

    err = enable_rotation_vector();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enable Rotation Vector failed");
        return err;
    }

    BaseType_t ok = xTaskCreate(imu_task, "imu", IMU_TASK_STACK, NULL,
                                IMU_TASK_PRIO, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "IMU up: BNO085 via SH-2 (I2C%d addr=0x%02X SDA=%d SCL=%d)",
             IMU_I2C_PORT, IMU_I2C_ADDR, IMU_I2C_SDA_GPIO, IMU_I2C_SCL_GPIO);
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
