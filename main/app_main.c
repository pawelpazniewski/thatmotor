#include "blackbox_recorder.h"
#include "control_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps.h"
#include "http_server.h"
#include "imu.h"
#include "nvs_store.h"
#include "pwm_out.h"
#include "rc_capture.h"
#include "settings_model.h"
#include "settings_validate.h"
#include "usb_console.h"
#include "wifi_ap.h"

static const char *TAG = "app_main";

/* The 50 Hz control loop runs in its own task, NOT the main task, so it can be
 * isolated from the networking stack. CPU1 keeps it off CPU0 where the Wi-Fi
 * task is pinned, so a web-panel reload's network burst cannot starve the loop
 * past the 5 s Task WDT and force a reboot (which drops ARMED -> DISARMED and
 * stops the drive mid-run). The priority sits ABOVE the diagnostic workers
 * (gps/imu/blackbox/console at prio 2) so they can never preempt a control
 * cycle, and below the Wi-Fi task (prio ~23, on CPU0). */
#define CONTROL_TASK_STACK 4096
#define CONTROL_TASK_PRIO 10
#define CONTROL_TASK_CORE 1

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

/* Control-loop task entry: runs the never-returning 50 Hz loop. Pinned to CPU1
 * at CONTROL_TASK_PRIO by app_main. */
static void control_task(void *arg)
{
    (void)arg;
    control_loop_run(); /* never returns */
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

    /* Blackbox recorder: a diagnostic background task (prio 2) that logs
     * spot-lock sessions to the `spotlog` flash partition. OPTIONAL and entirely
     * OUTSIDE failsafe: a start error is logged but never aborts the boot, and
     * losing it has no effect on arming/steering/failsafe or the 50 Hz loop. */
    esp_err_t blackbox_err = blackbox_recorder_start();
    if (blackbox_err != ESP_OK) {
        ESP_LOGW(TAG, "blackbox recorder start failed (0x%x); continuing without logging",
                 blackbox_err);
    }

    /* USB Serial/JTAG console: a diagnostic REPL (prio 2) for ground-side
     * calibration (spotlog dump / params get / params set). OPTIONAL and
     * entirely OUTSIDE failsafe: a start error is logged but never aborts the
     * boot, and the console has no effect on arming/steering/failsafe. */
    esp_err_t console_err = usb_console_start();
    if (console_err != ESP_OK) {
        ESP_LOGW(TAG, "USB console start failed (0x%x); continuing without console",
                 console_err);
    }

    /* Hand the 50 Hz loop to a dedicated CPU1 task (see CONTROL_TASK_* above) so
     * it is isolated from the Wi-Fi/HTTP stack on CPU0. The main task then
     * returns and self-deletes; the control task owns the loop from here. */
    ESP_LOGI(TAG, "control loop initialised; starting 50 Hz task on CPU%d (DISARMED)",
             CONTROL_TASK_CORE);
    BaseType_t ok = xTaskCreatePinnedToCore(control_task, "control",
                                            CONTROL_TASK_STACK, NULL,
                                            CONTROL_TASK_PRIO, NULL,
                                            CONTROL_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create control task; rebooting to safe state");
        esp_restart();
    }
}
