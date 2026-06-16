#include "esp_log.h"
#include "esp_system.h"
#include "pwm_out.h"

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

    /* Safe outputs FIRST, ahead of any RC/state-machine/web init (added in
     * later phases). This is the boot-to-safe-state placeholder for Phase 0. */
    enter_safe_outputs();

    ESP_LOGI(TAG, "Phase 0 boot complete; idling in safe state");
}
