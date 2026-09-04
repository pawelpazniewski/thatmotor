#include "blackbox_recorder.h"

#include "blackbox.h"
#include "blackbox_reason.h"
#include "blackbox_record.h"
#include "blackbox_region.h"
#include "blackbox_sampler.h"
#include "control_loop.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings_model.h"
#include "signal_chain.h"

static const char *TAG = "blackbox";

/* Low priority so the recorder can never preempt or stall the 50 Hz control
 * loop; the blackbox is diagnostic and entirely outside failsafe. */
#define BLACKBOX_TASK_STACK 3072
#define BLACKBOX_TASK_PRIO 2

/* CH3 spot-lock switch "high" threshold (midpoint of a 2-position switch). A
 * local copy of the control loop's SPOT_LOCK_CH3_THRESHOLD_US: a duplicated
 * constant is cheaper than coupling the diagnostic recorder to the loop's
 * internals (duplication < dependency). */
#define BLACKBOX_CH3_HIGH_US 1500U

/* Cap for the idle "ms since last sample" accumulator so it never overflows the
 * uint16 during a long OFF gap; well above the heartbeat, so capping is inert
 * for the adaptive-rate decision. */
#define BLACKBOX_MS_SINCE_CAP 60000U

/* Session + adaptive-rate state, owned solely by the recorder task (single
 * writer). Carries the prev-tick context the pure sampler/reason need. */
static uint8_t s_prev_substate;     /* spot-lock substate on the previous tick */
static uint8_t s_prev_source;       /* target source on the previous tick */
static uint8_t s_prev_sm_state;     /* control state on the previous tick */
static uint32_t s_session_seq;      /* monotonic session id, ++ per START */
static uint32_t s_session_start_ms; /* clock at session start (for sample t_ms) */
static uint16_t s_ms_since_sample;  /* elapsed since the last written record */
static uint16_t s_last_err_m;       /* position error at the last written sample */
static uint16_t s_off_tail_left;    /* post-OFF tail samples still owed */
static uint8_t s_tail_end_reason;   /* reason latched at the drop, reused per tail */
static uint32_t s_last_attempt_seq; /* last spot_lock_attempt_seq acted on */

/* Monotonic milliseconds (esp_timer is monotonic). */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Which source owns the target this tick: goto outranks a CH3 hold. */
static uint8_t derive_source(const control_loop_snapshot *s)
{
    if (s->goto_state != BLACKBOX_SPOT_LOCK_OFF) {
        return BLACKBOX_SRC_GOTO;
    }
    if (s->spot_lock_state != BLACKBOX_SPOT_LOCK_OFF) {
        return BLACKBOX_SRC_HOLD;
    }
    return BLACKBOX_SRC_NONE;
}

/* Infer why the session dropped to OFF, from the snapshot + active params. */
static uint8_t compute_end_reason(const control_loop_snapshot *s,
                                  const settings_params *p, uint8_t prev_source)
{
    bool is_failsafe = s->state == SM_STATE_FAILSAFE;
    bool is_armed = s->state == SM_STATE_ARMED;
    bool sticks_neutral =
        throttle_is_neutral(s->ch2_us, p) && steer_is_neutral(s->ch1_us, p);
    bool ch3_high = s->ch3_us > BLACKBOX_CH3_HIGH_US;
    bool was_goto = prev_source == BLACKBOX_SRC_GOTO;
    return (uint8_t)blackbox_end_reason_decide(is_failsafe, is_armed,
                                               sticks_neutral, ch3_high, was_goto);
}

/* Build the session header. The hold target is the goto point for a goto
 * session, otherwise the boat's own fix at session start (a CH3 "here" hold). */
static void fill_header(const control_loop_snapshot *snap,
                        const settings_params *params, uint8_t source,
                        blackbox_session_header *header)
{
    header->session_seq = s_session_seq;
    if (source == BLACKBOX_SRC_GOTO) {
        header->target_lat_e7 = snap->goto_target_lat_e7;
        header->target_lon_e7 = snap->goto_target_lon_e7;
    } else {
        header->target_lat_e7 = snap->gps_lat_e7;
        header->target_lon_e7 = snap->gps_lon_e7;
    }
    header->deadband_m = params->spot_lock_deadband_m;
    header->max_throttle_pct = params->spot_lock_max_throttle_pct;
    header->throttle_gain = params->spot_lock_throttle_gain;
    header->servo_gain = params->spot_lock_servo_gain;
    header->start_ms = s_session_start_ms;
}

/* Build a sample from the snapshot plus the derived source/end-reason. */
static void fill_sample(const control_loop_snapshot *s, uint8_t source,
                        uint8_t end_reason, blackbox_sample *o)
{
    o->t_ms = now_ms() - s_session_start_ms;
    o->substate = s->spot_lock_state;
    o->sm_state = (uint8_t)s->state;
    o->source = source;
    o->end_reason = end_reason;
    o->arm_reason = (uint8_t)s->arm_reason;
    o->err_m = s->spot_lock_err_m;
    o->bearing_deg10 = s->spot_lock_bearing_deg10;
    o->heading_deg10 = s->imu_heading_deg10;
    o->servo_us = (uint16_t)s->servo_us;
    o->esc_us = (uint16_t)s->esc_us;
    o->ch1_us = (uint16_t)s->ch1_us;
    o->ch2_us = (uint16_t)s->ch2_us;
    o->ch3_us = (uint16_t)s->ch3_us;
    o->ch4_us = (uint16_t)s->ch4_us;
    o->lat_e7 = s->gps_lat_e7;
    o->lon_e7 = s->gps_lon_e7;
    o->sats = s->gps_sats;
    o->speed_cms = s->gps_speed_cms;
    o->imu_calib = s->imu_calib;
    o->gps_fix = s->gps_fix;
    o->imu_ok = s->imu_ok;
    o->rc_valid = s->rc_valid;
    o->gps_fresh = s->gps_fresh;
    o->link_fresh = s->app_link_fresh;
    o->goto_owns = s->goto_state != BLACKBOX_SPOT_LOCK_OFF;
    o->arrived = s->goto_arrived;
}

/* Build an attempt record from the snapshot's latched entry-gate fields. */
static void fill_attempt(const control_loop_snapshot *s, blackbox_attempt *o)
{
    o->attempt_seq = s->spot_lock_attempt_seq;
    o->t_ms = now_ms();
    o->sm_state = (uint8_t)s->state;
    o->ok = s->spot_lock_attempt_ok;
    o->armed = s->spot_lock_attempt_armed;
    o->sticks_neutral = s->spot_lock_attempt_sticks_neutral;
    o->gps_fresh = s->spot_lock_attempt_gps_fresh;
    o->gps_fix = s->spot_lock_attempt_gps_fix;
    o->ch1_us = (uint16_t)s->ch1_us;
    o->ch2_us = (uint16_t)s->ch2_us;
    o->ch3_us = (uint16_t)s->ch3_us;
}

/* Encode + append, logging (never crashing) on a flash error. Best-effort: a
 * lost record must not take down the observer. */
static void append_or_warn(blackbox_status status, const char *what)
{
    if (status != BLACKBOX_OK) {
        ESP_LOGW(TAG, "%s append failed (status %d)", what, status);
    }
}

static void write_header(const control_loop_snapshot *snap, uint8_t source)
{
    settings_params params;
    control_loop_get_active_params(&params);

    s_session_seq++;
    s_session_start_ms = now_ms();

    blackbox_session_header header;
    fill_header(snap, &params, source, &header);

    uint8_t record[BLACKBOX_RECORD_SIZE];
    if (blackbox_record_encode_header(&header, record, sizeof(record)) !=
        BLACKBOX_REC_OK) {
        ESP_LOGW(TAG, "header encode failed");
        return;
    }
    append_or_warn(blackbox_append(record, sizeof(record)), "header");
}

static void write_sample(const control_loop_snapshot *snap, uint8_t source,
                         uint8_t end_reason)
{
    blackbox_sample sample;
    fill_sample(snap, source, end_reason, &sample);

    uint8_t record[BLACKBOX_RECORD_SIZE];
    if (blackbox_record_encode_sample(&sample, record, sizeof(record)) !=
        BLACKBOX_REC_OK) {
        ESP_LOGW(TAG, "sample encode failed");
        return;
    }
    append_or_warn(blackbox_append(record, sizeof(record)), "sample");
}

static void write_attempt(const control_loop_snapshot *snap)
{
    blackbox_attempt attempt;
    fill_attempt(snap, &attempt);

    uint8_t record[BLACKBOX_RECORD_SIZE];
    if (blackbox_record_encode_attempt(&attempt, record, sizeof(record)) !=
        BLACKBOX_REC_OK) {
        ESP_LOGW(TAG, "attempt encode failed");
        return;
    }
    append_or_warn(blackbox_append(record, sizeof(record)), "attempt");
}

/* One tick: peek the snapshot, run the pure sampler, and act. Keeps the
 * prev-tick context updated so the next tick sees the true transition. */
static void recorder_tick(void)
{
    control_loop_snapshot snap;
    control_loop_get_snapshot(&snap);
    settings_params params;
    control_loop_get_active_params(&params);

    uint8_t cur_substate = snap.spot_lock_state;
    uint8_t cur_source = derive_source(&snap);
    bool event = (cur_source != s_prev_source) ||
                 ((uint8_t)snap.state != s_prev_sm_state);

    blackbox_sampler_in in = {
        .prev_substate = s_prev_substate,
        .cur_substate = cur_substate,
        .ms_since_sample = s_ms_since_sample,
        .err_m = snap.spot_lock_err_m,
        .last_err_m = s_last_err_m,
        .event = event,
        .off_tail_left = s_off_tail_left,
        .attempt_seq = snap.spot_lock_attempt_seq,
        .last_attempt_seq = s_last_attempt_seq,
    };
    blackbox_sampler_out out = blackbox_sampler_step(&in);

    bool wrote = false;
    switch (out.action) {
    case BLACKBOX_ACTION_START_SESSION:
        write_header(&snap, cur_source);
        write_sample(&snap, cur_source, BLACKBOX_END_NONE);
        s_tail_end_reason = BLACKBOX_END_NONE;
        wrote = true;
        break;
    case BLACKBOX_ACTION_SAMPLE:
        write_sample(&snap, cur_source, BLACKBOX_END_NONE);
        wrote = true;
        break;
    case BLACKBOX_ACTION_SAMPLE_TAIL:
        /* On the drop tick (prev non-OFF) latch the reason once and reuse it for
         * the whole tail, so a stick recentering mid-tail cannot relabel it. The
         * tail samples belong to the session that dropped: log its prior source. */
        if (s_prev_substate != BLACKBOX_SPOT_LOCK_OFF) {
            s_tail_end_reason = compute_end_reason(&snap, &params, s_prev_source);
        }
        write_sample(&snap, s_prev_source, s_tail_end_reason);
        wrote = true;
        break;
    case BLACKBOX_ACTION_LOG_ATTEMPT:
        write_attempt(&snap);
        wrote = true;
        break;
    case BLACKBOX_ACTION_IDLE:
    default:
        break;
    }

    if (wrote) {
        s_ms_since_sample = 0U;
        s_last_err_m = snap.spot_lock_err_m;
    } else if (s_ms_since_sample < BLACKBOX_MS_SINCE_CAP) {
        s_ms_since_sample = (uint16_t)(s_ms_since_sample + BLACKBOX_TICK_MS);
    }
    s_off_tail_left = out.off_tail_left;
    s_last_attempt_seq = out.last_attempt_seq;
    s_prev_substate = cur_substate;
    s_prev_source = cur_source;
    s_prev_sm_state = (uint8_t)snap.state;
}

static void recorder_task(void *arg)
{
    (void)arg;
    s_prev_substate = BLACKBOX_SPOT_LOCK_OFF;
    s_prev_source = BLACKBOX_SRC_NONE;
    s_prev_sm_state = (uint8_t)SM_STATE_DISARMED;
    s_ms_since_sample = 0U;
    s_last_err_m = 0U;
    s_off_tail_left = 0U;
    s_tail_end_reason = BLACKBOX_END_NONE;
    s_last_attempt_seq = 0U;
    for (;;) {
        recorder_tick();
        vTaskDelay(pdMS_TO_TICKS(BLACKBOX_TICK_MS));
    }
}

esp_err_t blackbox_recorder_start(void)
{
    blackbox_status init = blackbox_init();
    if (init != BLACKBOX_OK) {
        ESP_LOGW(TAG, "blackbox_init failed (status %d)", init);
        return ESP_ERR_NOT_FOUND;
    }
    /* Continue session ids past whatever survived a reboot/brownout: the init
     * scan recovered the highest existing id, so the next START yields id + 1
     * instead of colliding with the previous outing's records. */
    s_session_seq = blackbox_resume_session_seq();
    BaseType_t ok = xTaskCreate(recorder_task, "blackbox", BLACKBOX_TASK_STACK,
                                NULL, BLACKBOX_TASK_PRIO, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "blackbox recorder up: ~%u ms tick, prio %d",
             BLACKBOX_TICK_MS, BLACKBOX_TASK_PRIO);
    return ESP_OK;
}
