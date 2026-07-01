#include "blackbox_recorder.h"

#include "blackbox.h"
#include "blackbox_record.h"
#include "blackbox_region.h"
#include "blackbox_sampler.h"
#include "control_loop.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings_model.h"

static const char *TAG = "blackbox";

/* Low priority so the recorder can never preempt or stall the 50 Hz control
 * loop; the blackbox is diagnostic and entirely outside failsafe. */
#define BLACKBOX_TASK_STACK 3072
#define BLACKBOX_TASK_PRIO 2

/* ~2 Hz peek of the telemetry snapshot. Losing a sample is acceptable; loop
 * jitter is not, so all flash I/O lives here, never in the control task. */
#define BLACKBOX_SAMPLE_PERIOD_MS 500U

/* Session state, owned solely by the recorder task (single writer). */
static uint8_t s_prev_substate;   /* spot-lock substate on the previous tick */
static uint32_t s_session_seq;    /* monotonic session id, ++ per START */
static uint32_t s_session_start_ms; /* clock at session start (for sample t_ms) */

/* Monotonic milliseconds (esp_timer is monotonic). */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Build the session header from the current GPS target and active settings.
 * The hold target is the boat's current GPS position at session start. */
static void fill_header(const control_loop_snapshot *snap,
                        const settings_params *params,
                        blackbox_session_header *header)
{
    header->session_seq = s_session_seq;
    header->target_lat_e7 = snap->gps_lat_e7;
    header->target_lon_e7 = snap->gps_lon_e7;
    header->deadband_m = params->spot_lock_deadband_m;
    header->max_throttle_pct = params->spot_lock_max_throttle_pct;
    header->throttle_gain = params->spot_lock_throttle_gain;
    header->servo_gain = params->spot_lock_servo_gain;
    header->start_ms = s_session_start_ms;
}

/* Build a per-cycle sample from the telemetry snapshot. Pulse widths are u32 in
 * the snapshot but always within a servo band, so they fit u16. */
static void fill_sample(const control_loop_snapshot *snap, blackbox_sample *sample)
{
    sample->t_ms = now_ms() - s_session_start_ms;
    sample->substate = snap->spot_lock_state;
    sample->err_m = snap->spot_lock_err_m;
    sample->bearing_deg10 = snap->spot_lock_bearing_deg10;
    sample->heading_deg10 = snap->imu_heading_deg10;
    sample->servo_us = (uint16_t)snap->servo_us;
    sample->esc_us = (uint16_t)snap->esc_us;
    sample->ch1_us = (uint16_t)snap->ch1_us;
    sample->ch2_us = (uint16_t)snap->ch2_us;
    sample->lat_e7 = snap->gps_lat_e7;
    sample->lon_e7 = snap->gps_lon_e7;
    sample->sats = snap->gps_sats;
    sample->speed_cms = snap->gps_speed_cms;
    sample->gps_fix = snap->gps_fix;
    sample->imu_ok = snap->imu_ok;
}

/* Encode + append, logging (never crashing) on a flash error. Best-effort: a
 * lost record must not take down the observer. */
static void append_or_warn(blackbox_status status, const char *what)
{
    if (status != BLACKBOX_OK) {
        ESP_LOGW(TAG, "%s append failed (status %d)", what, status);
    }
}

static void write_header(const control_loop_snapshot *snap)
{
    settings_params params;
    control_loop_get_active_params(&params);

    s_session_seq++;
    s_session_start_ms = now_ms();

    blackbox_session_header header;
    fill_header(snap, &params, &header);

    uint8_t record[BLACKBOX_RECORD_SIZE];
    if (blackbox_record_encode_header(&header, record, sizeof(record)) !=
        BLACKBOX_REC_OK) {
        ESP_LOGW(TAG, "header encode failed");
        return;
    }
    append_or_warn(blackbox_append(record, sizeof(record)), "header");
}

static void write_sample(const control_loop_snapshot *snap)
{
    blackbox_sample sample;
    fill_sample(snap, &sample);

    uint8_t record[BLACKBOX_RECORD_SIZE];
    if (blackbox_record_encode_sample(&sample, record, sizeof(record)) !=
        BLACKBOX_REC_OK) {
        ESP_LOGW(TAG, "sample encode failed");
        return;
    }
    append_or_warn(blackbox_append(record, sizeof(record)), "sample");
}

/* One tick: peek the snapshot, decide, and act. Keeps prev-substate updated so
 * the next tick sees the true transition. */
static void recorder_tick(void)
{
    control_loop_snapshot snap;
    control_loop_get_snapshot(&snap);

    blackbox_sampler_action action =
        blackbox_sampler_decide(s_prev_substate, snap.spot_lock_state);
    switch (action) {
    case BLACKBOX_ACTION_START_SESSION:
        write_header(&snap);
        write_sample(&snap); /* first sample of the session alongside the header */
        break;
    case BLACKBOX_ACTION_SAMPLE:
        write_sample(&snap);
        break;
    case BLACKBOX_ACTION_CLOSE_SESSION:
    case BLACKBOX_ACTION_IDLE:
        break;
    }

    s_prev_substate = snap.spot_lock_state;
}

static void recorder_task(void *arg)
{
    (void)arg;
    s_prev_substate = BLACKBOX_SPOT_LOCK_OFF;
    for (;;) {
        recorder_tick();
        vTaskDelay(pdMS_TO_TICKS(BLACKBOX_SAMPLE_PERIOD_MS));
    }
}

esp_err_t blackbox_recorder_start(void)
{
    blackbox_status init = blackbox_init();
    if (init != BLACKBOX_OK) {
        ESP_LOGW(TAG, "blackbox_init failed (status %d)", init);
        return ESP_ERR_NOT_FOUND;
    }
    BaseType_t ok = xTaskCreate(recorder_task, "blackbox", BLACKBOX_TASK_STACK,
                                NULL, BLACKBOX_TASK_PRIO, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "blackbox recorder up: ~%u ms tick, prio %d",
             BLACKBOX_SAMPLE_PERIOD_MS, BLACKBOX_TASK_PRIO);
    return ESP_OK;
}
