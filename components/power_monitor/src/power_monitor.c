#include "power_monitor.h"

#include <stddef.h>

#define DEFAULT_WARN_MV 11800U
#define DEFAULT_CLEAR_MV 12200U
#define DEFAULT_WARN_FRAMES 150U  /* 3 s at the 50 Hz control-loop cadence */
#define DEFAULT_REST_FRAMES 100U  /* 2 s near neutral before rest estimate */
#define DEFAULT_FILTER_SHIFT 3U   /* 1/8 EMA: damp short throttle sags/spikes */
#define DEFAULT_REST_SHIFT 4U     /* slower 1/16 EMA for rest estimate */

static uint16_t ema_u16(uint16_t previous, uint16_t sample, uint8_t shift)
{
    if (shift == 0U) {
        return sample;
    }
    int32_t delta = (int32_t)sample - (int32_t)previous;
    return (uint16_t)((int32_t)previous + (delta >> shift));
}

power_monitor_cfg power_monitor_default_cfg(void)
{
    power_monitor_cfg cfg = {
        .warn_mv = DEFAULT_WARN_MV,
        .clear_mv = DEFAULT_CLEAR_MV,
        .warn_frames = DEFAULT_WARN_FRAMES,
        .rest_frames = DEFAULT_REST_FRAMES,
        .filtered_shift = DEFAULT_FILTER_SHIFT,
        .rest_shift = DEFAULT_REST_SHIFT,
    };
    return cfg;
}

void power_monitor_init(power_monitor_state *state)
{
    if (state == NULL) {
        return;
    }
    *state = (power_monitor_state){0};
}

static void update_warning(power_monitor_state *state, const power_monitor_cfg *cfg)
{
    if (state->filtered_mv < cfg->warn_mv) {
        if (state->low_frames < cfg->warn_frames) {
            state->low_frames++;
        }
        if (state->low_frames >= cfg->warn_frames) {
            state->warning_active = true;
        }
        return;
    }

    if (state->filtered_mv >= cfg->clear_mv) {
        state->low_frames = 0;
        state->warning_active = false;
    }
}

static void update_rest_estimate(power_monitor_state *state,
                                 const power_monitor_cfg *cfg,
                                 bool esc_near_neutral)
{
    if (!esc_near_neutral) {
        state->neutral_frames = 0;
        return;
    }
    if (state->neutral_frames < cfg->rest_frames) {
        state->neutral_frames++;
    }
    if (state->neutral_frames >= cfg->rest_frames) {
        state->rest_estimate_mv = ema_u16(state->rest_estimate_mv,
                                          state->filtered_mv, cfg->rest_shift);
    }
}

power_monitor_output power_monitor_update(power_monitor_state *state,
                                          const power_monitor_cfg *cfg,
                                          uint16_t sample_mv,
                                          bool esc_near_neutral)
{
    if (state == NULL || cfg == NULL) {
        return (power_monitor_output){0};
    }

    state->instant_mv = sample_mv;
    if (!state->initialized) {
        state->initialized = true;
        state->filtered_mv = sample_mv;
        state->rest_estimate_mv = sample_mv;
    } else {
        state->filtered_mv = ema_u16(state->filtered_mv, sample_mv,
                                     cfg->filtered_shift);
    }

    update_warning(state, cfg);
    update_rest_estimate(state, cfg, esc_near_neutral);

    return (power_monitor_output){
        .instant_mv = state->instant_mv,
        .filtered_mv = state->filtered_mv,
        .rest_estimate_mv = state->rest_estimate_mv,
        .warning_active = state->warning_active,
    };
}
