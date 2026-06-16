#include "control_loop.h"

#include "commit_debounce.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "loop_step.h"
#include "nvs_store.h"
#include "pwm_out.h"
#include "rc_capture.h"
#include "rc_validity.h"

static const char *TAG = "control_loop";

/* RC validity thresholds. The accepted pulse band brackets the 1000..2000 us
 * RC range with margin; the frame period is a placeholder pending the HW
 * measurement (kontekst: do NOT assume 20 ms), with a wide tolerance so a
 * provisional value never spuriously fails. edge_timeout drives failsafe. */
#define RC_WIDTH_MIN_US 800U
#define RC_WIDTH_MAX_US 2200U
#define RC_PERIOD_EXPECTED_US 20000U
#define RC_PERIOD_TOL_US 8000U

/* Active params, owned exclusively by this loop (SI-6 single writer). */
static settings_params s_params;
static loop_state s_loop;
static loop_validity_cfg s_validity_cfg;
static QueueHandle_t s_pending_queue;
static commit_debounce_state s_commit;

/* Monotonic milliseconds for the commit debounce (esp_timer is monotonic). */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Build the per-channel validity config. edge_timeout comes from the configured
 * failsafe timeout so RC loss is caught on the same budget the operator tuned. */
static rc_channel_cfg make_channel_cfg(const settings_params *params)
{
    rc_channel_cfg cfg = {
        .width_min_us = RC_WIDTH_MIN_US,
        .width_max_us = RC_WIDTH_MAX_US,
        .period_expected_us = RC_PERIOD_EXPECTED_US,
        .period_tol_us = RC_PERIOD_TOL_US,
        .edge_timeout_us = (uint32_t)params->failsafe_timeout_ms * 1000U,
    };
    return cfg;
}

static void rebuild_validity_cfg(const settings_params *params)
{
    s_validity_cfg.ch1 = make_channel_cfg(params);
    s_validity_cfg.ch2 = make_channel_cfg(params);
}

esp_err_t control_loop_init(const settings_params *initial)
{
    if (initial == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_params = *initial;
    rebuild_validity_cfg(&s_params);
    loop_state_init(&s_loop, &s_params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    commit_debounce_init(&s_commit, COMMIT_DEBOUNCE_DEFAULT_MS);

    s_pending_queue = xQueueCreate(1, sizeof(settings_params));
    if (s_pending_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t control_loop_post_pending(const settings_params *pending)
{
    if (s_pending_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pending == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xQueueOverwrite(s_pending_queue, pending);
    return ESP_OK;
}

/* Apply a staged pending params set ONLY while DISARMED, re-checking the state
 * at apply time (TOCTOU). The loop is the single writer of active params. */
static void maybe_apply_pending(void)
{
    settings_params pending;
    if (xQueuePeek(s_pending_queue, &pending, 0) != pdTRUE) {
        return;
    }
    if (!loop_should_apply_pending(s_loop.state)) {
        return; /* leave pending staged until DISARMED (TOCTOU re-check) */
    }
    /* Consume it now that we have committed to applying under DISARMED. */
    xQueueReceive(s_pending_queue, &pending, 0);
    s_params = pending;
    rebuild_validity_cfg(&s_params);
    /* Stage a delayed NVS commit: coalesce a slider burst into one flash write. */
    commit_debounce_mark_changed(&s_commit, now_ms());
    ESP_LOGI(TAG, "applied pending params (DISARMED)");
}

/* Commit active params to NVS only while DISARMED (R17) and once the debounce
 * window has elapsed since the last change. A failed write leaves the change
 * pending (dirty stays set) so the next eligible cycle retries. */
static void maybe_commit_params(void)
{
    if (!loop_should_apply_pending(s_loop.state)) {
        return; /* same DISARMED gate as apply: never persist while armed */
    }
    if (!commit_debounce_should_commit(&s_commit, now_ms(), false)) {
        return;
    }
    esp_err_t err = nvs_store_commit(&s_params);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS commit failed (0x%x), will retry", err);
        return;
    }
    commit_debounce_mark_committed(&s_commit);
    ESP_LOGI(TAG, "params committed to NVS");
}

/* Read the two control channels into the per-cycle input snapshot. */
static loop_inputs read_inputs(void)
{
    loop_inputs in = {0};
    rc_capture_read(RC_CAP_CH1, &in.ch1);
    rc_capture_read(RC_CAP_CH2, &in.ch2);
    in.now_ticks = rc_capture_now_ticks();
    in.ui_arm_request = false;   /* wired to the panel in Unit 10 */
    in.ui_disarm_request = false;
    return in;
}

static void run_one_cycle(void)
{
    maybe_apply_pending();

    loop_inputs in = read_inputs();
    loop_outputs out = loop_step(&in, &s_validity_cfg, &s_params, &s_loop);

    pwm_out_write_us(PWM_OUT_ESC, out.esc_us);
    pwm_out_write_us(PWM_OUT_SERVO, out.servo_us);

    /* Persist staged changes after actuators are driven (DISARMED + debounce). */
    maybe_commit_params();
}

void control_loop_run(void)
{
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_LOGI(TAG, "control loop running @ ~%u Hz", 1000U / CONTROL_LOOP_PERIOD_MS);

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        run_one_cycle();
        /* Feed the watchdog ONLY after a fully completed iteration. */
        esp_task_wdt_reset();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_LOOP_PERIOD_MS));
    }
}
