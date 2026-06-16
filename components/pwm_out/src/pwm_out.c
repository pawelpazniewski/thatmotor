#include "pwm_out.h"

#include "driver/ledc.h"
#include "esp_err.h"
#include "pwm_out_logic.h"
#include "pwm_us_to_duty.h"

/* The pure logic mirrors the channel count; keep the two definitions in sync. */
_Static_assert(PWM_OUT_LOGIC_CHANNEL_COUNT == PWM_OUT_CHANNEL_COUNT,
               "pwm_out_logic channel count out of sync with PwmOutChannel");

/* GPIO assignment (fixed pin map from the plan). */
#define PWM_OUT_SERVO_GPIO 18
#define PWM_OUT_ESC_GPIO 19

#define PWM_OUT_SPEED_MODE LEDC_LOW_SPEED_MODE
#define PWM_OUT_TIMER LEDC_TIMER_0

/* The single hard-clamp window shared by every output write (SI-3). */
static const PwmWindow PWM_OUT_WINDOW = {
    .min_us = PWM_OUT_MIN_US,
    .max_us = PWM_OUT_MAX_US,
};

static const ledc_channel_t CHANNEL_MAP[PWM_OUT_CHANNEL_COUNT] = {
    [PWM_OUT_SERVO] = LEDC_CHANNEL_0,
    [PWM_OUT_ESC] = LEDC_CHANNEL_1,
};

static const int GPIO_MAP[PWM_OUT_CHANNEL_COUNT] = {
    [PWM_OUT_SERVO] = PWM_OUT_SERVO_GPIO,
    [PWM_OUT_ESC] = PWM_OUT_ESC_GPIO,
};

static esp_err_t configure_channel(PwmOutChannel channel)
{
    ledc_channel_config_t cfg = {
        .gpio_num = GPIO_MAP[channel],
        .speed_mode = PWM_OUT_SPEED_MODE,
        .channel = CHANNEL_MAP[channel],
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PWM_OUT_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&cfg);
}

esp_err_t pwm_out_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = PWM_OUT_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_16_BIT,
        .timer_num = PWM_OUT_TIMER,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }

    for (int channel = 0; channel < PWM_OUT_CHANNEL_COUNT; channel++) {
        err = configure_channel((PwmOutChannel)channel);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t pwm_out_write_us(PwmOutChannel channel, uint32_t value_us)
{
    uint32_t duty = 0;
    PwmOutLogicResult result =
        pwm_out_resolve_duty(channel, value_us, PWM_OUT_WINDOW, &duty);
    if (result != PWM_OUT_LOGIC_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ledc_set_duty(PWM_OUT_SPEED_MODE, CHANNEL_MAP[channel], duty);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(PWM_OUT_SPEED_MODE, CHANNEL_MAP[channel]);
}
