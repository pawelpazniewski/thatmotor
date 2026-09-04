#include "rc_capture.h"

#include "cap_math.h"
#include "driver/mcpwm_cap.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_timer.h"

/* Ticks per microsecond in the rc_capture recency domain (12.5 ns/tick =>
 * 80 ticks/us). Recency timestamps (last_edge_ticks, rc_capture_now_ticks) live
 * in this domain so cap_ticks_to_us converts them consistently. */
#define RC_CAP_TICKS_PER_US 80U

/* Current recency tick: esp_timer microseconds scaled into the 80 MHz tick
 * domain, truncated to 32 bits so it wraps at 2^32 ticks (~53.6 s) exactly like
 * the hardware capture counter. esp_timer is ISR-safe and monotonic, so this
 * advances in real time even when RC edges stop (failsafe still fires). The
 * hardware cap_value is kept for width/period (relative, within-frame) where
 * the absolute epoch is irrelevant. */
static inline uint32_t rc_cap_now_ticks_raw(void)
{
    return (uint32_t)((uint64_t)esp_timer_get_time() * RC_CAP_TICKS_PER_US);
}

/* GPIO assignment (ESP32-S3 N16R8 pin map). The four RC channels sit in one
 * contiguous header block (16,17,18,8) so the receiver leads plug in side by
 * side. Any GPIO routes to MCPWM capture via the matrix. */
#define RC_CAP_CH1_GPIO 16
#define RC_CAP_CH2_GPIO 17
#define RC_CAP_CH3_GPIO 8
#define RC_CAP_CH4_GPIO 18

/* Capture timer at the APB clock: 80 MHz -> 12.5 ns/tick, matching cap_math.
 * Prescale 1 keeps the per-channel resolution at the full tick rate. */
#define RC_CAP_RESOLUTION_HZ 80000000U
#define RC_CAP_PRESCALE 1U

/* MCPWM capture groups. Group 0 has only 3 capture channels and is fully used by
 * CH1/CH2/CH4, so CH3 (diagnostic, future spot lock) lives on group 1's own
 * capture timer. Each used group gets exactly one capture timer in init. */
#define RC_CAP_GROUP_0 0
#define RC_CAP_GROUP_1 1
#define RC_CAP_GROUP_COUNT 2

static const int RC_CAP_GPIO_MAP[RC_CAP_CHANNEL_COUNT] = {
    [RC_CAP_CH1] = RC_CAP_CH1_GPIO,
    [RC_CAP_CH2] = RC_CAP_CH2_GPIO,
    [RC_CAP_CH4] = RC_CAP_CH4_GPIO,
    [RC_CAP_CH3] = RC_CAP_CH3_GPIO,
};

/* Channel -> MCPWM group. CH1/CH2/CH4 fill group 0; CH3 sits alone on group 1. */
static const int RC_CAP_GROUP_MAP[RC_CAP_CHANNEL_COUNT] = {
    [RC_CAP_CH1] = RC_CAP_GROUP_0,
    [RC_CAP_CH2] = RC_CAP_GROUP_0,
    [RC_CAP_CH4] = RC_CAP_GROUP_0,
    [RC_CAP_CH3] = RC_CAP_GROUP_1,
};

/* Per-channel capture state, updated only from the capture ISR callback. */
typedef struct {
    rc_channel_sample sample;
    uint32_t rising_edge_ticks;
    uint32_t prev_rising_edge_ticks;
    bool rising_pending;
    bool prev_rising_valid;
} rc_cap_state;

static rc_cap_state s_state[RC_CAP_CHANNEL_COUNT];

/* On a rising edge: record the timestamp, derive period from the previous
 * rising edge. On a falling edge: derive pulse width from the pending rising
 * edge. Runs in ISR context; touches only this channel's state. */
static bool IRAM_ATTR on_cap(mcpwm_cap_channel_handle_t channel,
                             const mcpwm_capture_event_data_t *edata,
                             void *user_ctx)
{
    (void)channel;
    rc_cap_state *state = (rc_cap_state *)user_ctx;
    uint32_t ticks = edata->cap_value;

    if (edata->cap_edge == MCPWM_CAP_EDGE_POS) {
        if (state->prev_rising_valid) {
            state->sample.period_us =
                cap_period_us(ticks, state->prev_rising_edge_ticks);
        }
        state->prev_rising_edge_ticks = ticks;
        state->prev_rising_valid = true;
        state->rising_edge_ticks = ticks;
        state->rising_pending = true;
        /* Recency timestamp in the esp_timer-derived tick domain (same domain as
         * rc_capture_now_ticks), NOT the raw hardware cap_value: that lets the
         * loop compute edge age wrap-safely against a clock that keeps advancing
         * when edges stop. Width/period below stay on the hardware cap_value. */
        state->sample.last_edge_ticks = rc_cap_now_ticks_raw();
        return false;
    }

    if (state->rising_pending) {
        state->sample.width_us =
            cap_ticks_to_us(cap_ticks_elapsed(ticks, state->rising_edge_ticks));
        state->sample.edge_seen = true;
        state->rising_pending = false;
    }
    return false;
}

static esp_err_t configure_channel(mcpwm_cap_timer_handle_t timer,
                                   RcCaptureChannel channel)
{
    /* Capture both edges: rising starts the pulse/sets the period reference,
     * falling closes the pulse width. No internal pull configured: the RC
     * receiver drives the lines actively, so we leave the S3 I/O pins floating
     * from the controller side. A weak internal pull-down was tried here to
     * make a disconnected/floating line settle low instead of picking up
     * noise, but it regressed RC-loss failsafe on this receiver (which goes
     * high-Z on signal loss rather than holding a driven neutral): the pull-down
     * turned the floating line into something that read as plausible RC frames
     * instead of the clearly-implausible noise channel_valid used to reject, so
     * failsafe stopped tripping on transmitter-off. Reverted; do not re-add
     * without re-validating actual TX-off failsafe entry on hardware. */
    mcpwm_capture_channel_config_t cfg = {
        .gpio_num = RC_CAP_GPIO_MAP[channel],
        .prescale = RC_CAP_PRESCALE,
        .flags = {.pos_edge = true, .neg_edge = true},
    };

    mcpwm_cap_channel_handle_t handle = NULL;
    esp_err_t err = mcpwm_new_capture_channel(timer, &cfg, &handle);
    if (err != ESP_OK) {
        return err;
    }

    mcpwm_capture_event_callbacks_t cbs = {.on_cap = on_cap};
    err = mcpwm_capture_channel_register_event_callbacks(handle, &cbs,
                                                         &s_state[channel]);
    if (err != ESP_OK) {
        return err;
    }
    return mcpwm_capture_channel_enable(handle);
}

/* Create one capture timer for the given MCPWM group at the shared resolution. */
static esp_err_t create_group_timer(int group_id, mcpwm_cap_timer_handle_t *out)
{
    mcpwm_capture_timer_config_t timer_cfg = {
        .group_id = group_id,
        .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
        .resolution_hz = RC_CAP_RESOLUTION_HZ,
    };
    return mcpwm_new_capture_timer(&timer_cfg, out);
}

esp_err_t rc_capture_init(void)
{
    /* One capture timer per used MCPWM group (group 0: CH1/CH2/CH4, group 1: CH3).
     * Channels attach to the timer of their mapped group. */
    mcpwm_cap_timer_handle_t timers[RC_CAP_GROUP_COUNT] = {0};
    for (int group = 0; group < RC_CAP_GROUP_COUNT; group++) {
        esp_err_t err = create_group_timer(group, &timers[group]);
        if (err != ESP_OK) {
            return err;
        }
    }

    for (int channel = 0; channel < RC_CAP_CHANNEL_COUNT; channel++) {
        mcpwm_cap_timer_handle_t timer = timers[RC_CAP_GROUP_MAP[channel]];
        esp_err_t err = configure_channel(timer, (RcCaptureChannel)channel);
        if (err != ESP_OK) {
            return err;
        }
    }

    for (int group = 0; group < RC_CAP_GROUP_COUNT; group++) {
        esp_err_t err = mcpwm_capture_timer_enable(timers[group]);
        if (err != ESP_OK) {
            return err;
        }
        err = mcpwm_capture_timer_start(timers[group]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t rc_capture_read(RcCaptureChannel channel, rc_channel_sample *out)
{
    if (channel >= RC_CAP_CHANNEL_COUNT || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_state[channel].sample;
    return ESP_OK;
}

uint32_t rc_capture_now_ticks(void)
{
    return rc_cap_now_ticks_raw();
}
