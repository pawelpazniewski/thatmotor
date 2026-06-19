#include "control_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "gps.h"
#include "http_server.h"
#include "imu.h"
#include "nvs_store.h"
#include "pwm_out.h"
#include "rc_capture.h"
#include "settings_model.h"
#include "settings_validate.h"
#include "wifi_ap.h"

static const char *TAG = "app_main";

/* Map a reset reason to a short human-readable label for the boot log. */
static const char *reset_reason_label(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "power-on";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "int-watchdog";
    case ESP_RST_TASK_WDT:
        return "task-watchdog";
    case ESP_RST_WDT:
        return "other-watchdog";
    case ESP_RST_BROWNOUT:
        return "brownout";
    default:
        return "unknown";
    }
}

/* Bring both actuators to their safe neutral/center pulse before anything else
 * runs. SI-1: every boot path lands in a safe output state. Fail-fast on any
 * LEDC error so a broken output cannot silently leave the actuators floating. */
static void enter_safe_outputs(void)
{
    ESP_ERROR_CHECK(pwm_out_init());
    ESP_ERROR_CHECK(pwm_out_write_us(PWM_OUT_ESC, PWM_OUT_NEUTRAL_US));
    ESP_ERROR_CHECK(pwm_out_write_us(PWM_OUT_SERVO, PWM_OUT_NEUTRAL_US));
    ESP_LOGI(TAG, "outputs in safe state: ESC+servo @ %u us", PWM_OUT_NEUTRAL_US);
}

void app_main(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "boot: reset reason = %s (%d)", reset_reason_label(reason), reason);

    /* Safe outputs FIRST, ahead of any RC/state-machine/web init. SI-1: every
     * boot path lands in a safe output state before signal acquisition starts. */
    enter_safe_outputs();

    /* RC signal acquisition (MCPWM capture) once outputs are safe. Fail-fast on
     * any driver error: a broken capture init must surface, not run silently. */
    ESP_ERROR_CHECK(rc_capture_init());
    ESP_LOGI(TAG, "RC capture started: CH1/CH2/CH4 (MCPWM)");

    /* Load persisted params from NVS BEFORE the loop starts. A corrupt/empty/
     * version-mismatched store falls back to conservative defaults inside
     * nvs_store_load (validated at read), so active params are never outside the
     * sanity window. The loop is the single writer of active params (SI-6) and
     * always boots in DISARMED (SI-1). This task becomes the control task and
     * never returns; the watchdog is fed only at the end of each completed cycle. */
    settings_params params;
    settings_validation_result load_result;
    ESP_ERROR_CHECK(nvs_store_load(&params, &load_result));
    ESP_LOGI(TAG, "settings loaded: source=%d calibrated=%d defaults_used=%d",
             load_result.source, load_result.calibrated, load_result.defaults_used);
    ESP_ERROR_CHECK(control_loop_init(&params, &load_result));

    /* Bring up connectivity BEFORE the control loop takes over this task and
     * never returns. wifi_ap_start fail-fasts if the AP would not be WPA2-PSK
     * (R14: never an open access point). The HTTP server + telemetry WS + params
     * API run on the system/server task; the control loop owns this task. */
    ESP_ERROR_CHECK(wifi_ap_start());
    ESP_ERROR_CHECK(http_server_start());
    ESP_LOGI(TAG, "AP + web panel up");

    /* GPS last, after the safe state + AP are up. It is OPTIONAL and entirely
     * OUTSIDE failsafe: a start error is logged but never aborts the boot, and
     * losing the GPS has no effect on arming/steering/failsafe. */
    esp_err_t gps_err = gps_start();
    if (gps_err != ESP_OK) {
        ESP_LOGW(TAG, "GPS start failed (0x%x); continuing without GPS", gps_err);
    }

    /* Compass (BNO085) is also OPTIONAL and entirely OUTSIDE failsafe: a start
     * error is logged but never aborts the boot, and losing it has no effect on
     * arming/steering/failsafe. */
    esp_err_t imu_err = imu_start();
    if (imu_err != ESP_OK) {
        ESP_LOGW(TAG, "IMU start failed (0x%x); continuing without compass", imu_err);
    }

    ESP_LOGI(TAG, "control loop initialised; entering 50 Hz loop (DISARMED)");
    control_loop_run();
}
