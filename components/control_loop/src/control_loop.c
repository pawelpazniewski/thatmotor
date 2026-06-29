#include "control_loop.h"

#include "ch4_switch.h"
#include "click_counter.h"
#include "commit_debounce.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gps.h"
#include "imu.h"
#include "led_driver.h"
#include "led_pattern.h"
#include "loop_step.h"
#include "nvs_store.h"
#include "pwm_out.h"
#include "rc_capture.h"
#include "rc_validity.h"
#include "signal_chain.h"

static const char *TAG = "control_loop";

/* RC validity thresholds. The accepted pulse band brackets the 1000..2000 us
 * RC range with margin. The frame period is validated against a BROAD band, not
 * an assumed 20 ms (kontekst: do NOT assume 20 ms): receivers run ~40-500 Hz, so
 * accept 2..30 ms and let width + edge recency + debounce reject garbage. A real
 * measured fast receiver (~3 ms / 330 Hz) passes; a stuck line does not.
 * edge_timeout drives failsafe. */
#define RC_WIDTH_MIN_US 800U
#define RC_WIDTH_MAX_US 2200U
#define RC_PERIOD_MIN_US 2000U
#define RC_PERIOD_MAX_US 30000U

/* Active params, owned exclusively by this loop (SI-6 single writer). */
static settings_params s_params;
static loop_state s_loop;
static loop_validity_cfg s_validity_cfg;
/* Persistent CH4 mode-button debounce/edge state (one frame's carry-over). */
static ch4_switch_state s_ch4_switch;
/* Persistent CH4 click-gesture accumulator (1 vs 3 clicks across frames). */
static click_counter_state s_click;
static QueueHandle_t s_pending_queue;
static QueueHandle_t s_ui_queue;
static commit_debounce_state s_commit;
/* Set by a panel "Save trim" to force the next eligible commit immediately
 * (bypass the debounce window); cleared once the write lands. */
static bool s_force_commit;

/* Load-time provenance flags (R16), surfaced verbatim to panel telemetry. */
static settings_validation_result s_load_flags;

/* Latest telemetry snapshot (lossy single slot, newest wins). The loop is the
 * sole writer; panel readers copy the struct best-effort. */
static control_loop_snapshot s_snapshot;

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
        .period_min_us = RC_PERIOD_MIN_US,
        .period_max_us = RC_PERIOD_MAX_US,
        .edge_timeout_us = (uint32_t)params->failsafe_timeout_ms * 1000U,
    };
    return cfg;
}

static void rebuild_validity_cfg(const settings_params *params)
{
    s_validity_cfg.ch1 = make_channel_cfg(params);
    s_validity_cfg.ch2 = make_channel_cfg(params);
}

/* Build the CH4 button config from the active params. The sanity band reuses
 * the same accepted RC pulse window as the control channels. */
static ch4_switch_cfg make_ch4_switch_cfg(const settings_params *params)
{
    ch4_switch_cfg cfg = {
        .threshold_us = params->ch4_switch_threshold_us,
        .sanity_min_us = RC_WIDTH_MIN_US,
        .sanity_max_us = RC_WIDTH_MAX_US,
        .debounce_frames = CH4_SWITCH_DEBOUNCE_FRAMES,
    };
    return cfg;
}

/* Convert the configured click window (ms) into whole control frames, rounding
 * UP so the window is never shorter than configured. The single shared
 * CONTROL_LOOP_PERIOD_MS named constant defines the cycle, so no period is
 * hardcoded here. A 0 ms window would collapse to 0 frames; the validated range
 * floors click_window_ms well above 0, so the result is always >= 1. */
static uint16_t click_window_frames(const settings_params *params)
{
    uint32_t ms = params->click_window_ms;
    uint32_t frames = (ms + CONTROL_LOOP_PERIOD_MS - 1U) / CONTROL_LOOP_PERIOD_MS;
    return (uint16_t)frames;
}

/* Map a click gesture to the per-state CH4 intent while DISARMED: a single
 * click arms (through the state machine's gate), a triple click deploys. */
static void apply_disarmed_gesture(click_gesture gesture, loop_inputs *in)
{
    if (gesture == CLICK_SINGLE) {
        in->ui_arm_request = true;
    } else if (gesture == CLICK_TRIPLE) {
        in->deploy_request = true;
    }
}

/* Fold the CH4 momentary button's click gestures into this cycle's intents.
 * Each press fires exactly one accepted edge ("click"). The mapping is by the
 * CURRENT state, so panel and CH4 stay interchangeable and CH4 never forces a
 * state out of FAILSAFE/calibration:
 *   ARMED    -> any single click is an IMMEDIATE disarm (no window wait); the
 *               click accumulator is reset so it cannot also deploy.
 *   DISARMED -> 1 click arms (gated), 3 clicks deploy, 2 clicks do nothing.
 *   DEPLOY   -> 3 clicks stow (-> DISARMED); 1-2 clicks do NOTHING (no
 *               accidental exit while the motor is raised).
 *   else     -> ignored, accumulator reset.
 * The OR is intentional: panel requests were applied upstream and CH4 only adds
 * intent, never clears it. Disabled -> CH4 has no influence. Arming still passes
 * the full safety gate in the state machine. */
static void apply_ch4_switch(loop_inputs *in)
{
    if (!s_params.ch4_mode_switch_enabled) {
        return;
    }
    rc_channel_sample ch4 = {0};
    rc_capture_read(RC_CAP_CH4, &ch4);
    ch4_switch_cfg cfg = make_ch4_switch_cfg(&s_params);
    bool click = ch4_switch_update(&s_ch4_switch, &ch4, &cfg) != CH4_SWITCH_NONE;
    uint16_t window = click_window_frames(&s_params);

    if (s_loop.state == SM_STATE_ARMED) {
        if (click) {
            in->ui_disarm_request = true; /* instant disarm, no window wait */
        }
        click_counter_reset(&s_click);
        return;
    }
    if (s_loop.state == SM_STATE_DISARMED) {
        apply_disarmed_gesture(click_counter_update(&s_click, click, window), in);
        return;
    }
    if (s_loop.state == SM_STATE_DEPLOY) {
        if (click_counter_update(&s_click, click, window) == CLICK_TRIPLE) {
            in->stow_request = true;
        }
        return;
    }
    click_counter_reset(&s_click); /* FAILSAFE / calibration: ignore CH4 */
}

esp_err_t control_loop_init(const settings_params *initial,
                            const settings_validation_result *load_result)
{
    if (initial == NULL || load_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_params = *initial;
    s_load_flags = *load_result;
    rebuild_validity_cfg(&s_params);
    loop_state_init(&s_loop, &s_params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    ch4_switch_init(&s_ch4_switch);
    click_counter_reset(&s_click);
    commit_debounce_init(&s_commit, COMMIT_DEBOUNCE_DEFAULT_MS);
    s_force_commit = false;

    s_pending_queue = xQueueCreate(1, sizeof(settings_params));
    if (s_pending_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_ui_queue = xQueueCreate(1, sizeof(control_loop_ui_events));
    if (s_ui_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void control_loop_get_snapshot(control_loop_snapshot *out)
{
    if (out == NULL) {
        return;
    }
    *out = s_snapshot; /* best-effort copy of the newest slot */
}

void control_loop_get_active_params(settings_params *out)
{
    if (out == NULL) {
        return;
    }
    *out = s_params;
}

esp_err_t control_loop_post_ui_events(const control_loop_ui_events *events)
{
    if (s_ui_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (events == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xQueueOverwrite(s_ui_queue, events);
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
    if (!commit_debounce_should_commit(&s_commit, now_ms(), s_force_commit)) {
        return;
    }
    esp_err_t err = nvs_store_commit(&s_params);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS commit failed (0x%x), will retry", err);
        return;
    }
    commit_debounce_mark_committed(&s_commit);
    s_force_commit = false;
    ESP_LOGI(TAG, "params committed to NVS");
}

/* Apply the panel's live servo-trim controls, DISARMED only (SI-6: the loop is
 * the single writer of s_params). Step Left/Right nudge servo_trim_us by one
 * click and stage a debounced NVS commit; Save forces an immediate commit on the
 * next eligible cycle. Outside DISARMED every trim event is ignored. */
static void apply_trim_events(const control_loop_ui_events *ev)
{
    if (!loop_should_apply_pending(s_loop.state)) {
        return;
    }
    if (ev->trim_left || ev->trim_right) {
        int dir = ev->trim_right ? 1 : -1;
        s_params.servo_trim_us = servo_trim_stepped(
            s_params.servo_trim_us, dir, SERVO_TRIM_STEP_US, SERVO_TRIM_MAX_US);
        commit_debounce_mark_changed(&s_commit, now_ms());
    }
    if (ev->trim_save) {
        /* Ensure there is something to persist, then force an immediate commit
         * (skip the debounce window) on the next eligible cycle. */
        commit_debounce_mark_changed(&s_commit, now_ms());
        s_force_commit = true;
    }
}

/* Drain the latest staged UI events into the per-cycle inputs (edge semantics:
 * each posted set is consumed once). Absent any post, all events are inert. */
static void apply_ui_events(loop_inputs *in)
{
    control_loop_ui_events ev;
    if (xQueueReceive(s_ui_queue, &ev, 0) != pdTRUE) {
        return;
    }
    in->ui_arm_request = ev.arm_request;
    in->ui_disarm_request = ev.disarm_request;
    in->ui_calib_request = ev.calib_request;
    in->ui_calib_confirm = ev.calib_confirm;
    in->deploy_request = ev.deploy_request;
    in->stow_request = ev.stow_request;
    in->calib_event = ev.calib_event;
    apply_trim_events(&ev);
}

/* Read the two control channels into the per-cycle input snapshot. */
static loop_inputs read_inputs(void)
{
    loop_inputs in = {0};
    rc_capture_read(RC_CAP_CH1, &in.ch1);
    rc_capture_read(RC_CAP_CH2, &in.ch2);
    in.now_ticks = rc_capture_now_ticks();
    /* Panel mailbox first, then OR in the CH4 position-switch intent so CH4
     * augments (never overrides) an arm/disarm request from the panel. */
    apply_ui_events(&in);
    apply_ch4_switch(&in);
    return in;
}

/* Publish the newest telemetry snapshot for the panel (lossy single slot). */
static void publish_snapshot(const loop_inputs *in, const loop_outputs *out)
{
    rc_channel_sample ch4 = {0};
    rc_capture_read(RC_CAP_CH4, &ch4);
    rc_channel_sample ch3 = {0};
    rc_capture_read(RC_CAP_CH3, &ch3);
    s_snapshot.state = out->telemetry.state;
    s_snapshot.arm_reason = out->telemetry.arm_reason;
    s_snapshot.rc_valid = out->telemetry.rc_valid;
    s_snapshot.ch1_us = in->ch1.width_us;
    s_snapshot.ch2_us = in->ch2.width_us;
    s_snapshot.ch4_us = ch4.width_us;
    s_snapshot.ch3_us = ch3.width_us;
    /* DIAG: surface the hidden validity inputs in the panel (period + per-channel
     * pass/fail) so RC can be diagnosed over WiFi without USB attached. */
    s_snapshot.ch1_period_us = in->ch1.period_us;
    s_snapshot.ch2_period_us = in->ch2.period_us;
    s_snapshot.ch1_valid = channel_valid(&in->ch1, in->now_ticks,
                                         &s_validity_cfg.ch1);
    s_snapshot.ch2_valid = channel_valid(&in->ch2, in->now_ticks,
                                         &s_validity_cfg.ch2);
    s_snapshot.servo_us = out->servo_us;
    s_snapshot.esc_us = out->esc_us;
    s_snapshot.servo_trim_us = s_params.servo_trim_us;
    s_snapshot.source = s_load_flags.source;
    s_snapshot.settings_valid = s_load_flags.settings_valid;
    s_snapshot.calibrated = s_load_flags.calibrated;
    s_snapshot.defaults_used = s_load_flags.defaults_used;
    s_snapshot.nvs_error = s_load_flags.nvs_error;
    /* GPS is the ONLY non-control touch point: copy the diagnostic fix into the
     * snapshot for the panel. Outside failsafe; never feeds a control decision. */
    gps_state g;
    gps_get_state(&g);
    s_snapshot.gps_fix = g.fix;
    s_snapshot.gps_sats = g.sats;
    s_snapshot.gps_lat_e7 = g.lat_e7;
    s_snapshot.gps_lon_e7 = g.lon_e7;
    s_snapshot.gps_speed_cms = g.speed_cms;
    /* IMU/compass: same diagnostic-only contract as GPS. Outside failsafe;
     * never feeds a control decision. */
    imu_state m;
    imu_get_state(&m);
    s_snapshot.imu_ok = m.ok;
    s_snapshot.imu_heading_deg10 = m.heading_deg10;
    s_snapshot.imu_calib = m.calib;
}

/* Drive the RGB status LED for this cycle from the pure pattern (Unit 11). */
static void drive_led(sm_state state)
{
    led_driver_show(led_pattern_color(state, s_load_flags.calibrated, now_ms()));
}

static void run_one_cycle(void)
{
    maybe_apply_pending();

    loop_inputs in = read_inputs();
    loop_outputs out = loop_step(&in, &s_validity_cfg, &s_params, &s_loop);

    pwm_out_write_us(PWM_OUT_ESC, out.esc_us);
    pwm_out_write_us(PWM_OUT_SERVO, out.servo_us);

    publish_snapshot(&in, &out);
    drive_led(out.telemetry.state);

    /* Persist staged changes after actuators are driven (DISARMED + debounce). */
    maybe_commit_params();
}

void control_loop_run(void)
{
    ESP_ERROR_CHECK(led_driver_init());
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
