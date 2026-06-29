#include "led_driver.h"

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

/* WS2812 timing in a 10 MHz RMT domain (0.1 us/tick): T0H=0.3us, T0L=0.9us,
 * T1H=0.9us, T1L=0.3us — within the WS2812B +/-150 ns window. The >50 us
 * idle-low gap between 50 Hz frames latches the colour, so no explicit reset
 * symbol is needed. */
#define LED_RMT_RESOLUTION_HZ 10000000U
#define LED_RMT_T0H_TICKS 3U
#define LED_RMT_T0L_TICKS 9U
#define LED_RMT_T1H_TICKS 9U
#define LED_RMT_T1L_TICKS 3U
#define LED_RMT_MEM_BLOCK_SYMBOLS 64U
#define LED_RMT_QUEUE_DEPTH 4U
#define LED_RMT_DONE_TIMEOUT_MS 10

static rmt_channel_handle_t s_channel = NULL;
static rmt_encoder_handle_t s_encoder = NULL;

esp_err_t led_driver_init(void)
{
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = LED_DRIVER_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RMT_RESOLUTION_HZ,
        .mem_block_symbols = LED_RMT_MEM_BLOCK_SYMBOLS,
        .trans_queue_depth = LED_RMT_QUEUE_DEPTH,
    };
    esp_err_t err = rmt_new_tx_channel(&chan_cfg, &s_channel);
    if (err != ESP_OK) {
        return err;
    }

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {.level0 = 1,
                 .duration0 = LED_RMT_T0H_TICKS,
                 .level1 = 0,
                 .duration1 = LED_RMT_T0L_TICKS},
        .bit1 = {.level0 = 1,
                 .duration0 = LED_RMT_T1H_TICKS,
                 .level1 = 0,
                 .duration1 = LED_RMT_T1L_TICKS},
        .flags = {.msb_first = 1},
    };
    err = rmt_new_bytes_encoder(&enc_cfg, &s_encoder);
    if (err != ESP_OK) {
        return err;
    }

    err = rmt_enable(s_channel);
    if (err != ESP_OK) {
        return err;
    }

    led_driver_show((LedColor){.r = 0U, .g = 0U, .b = 0U});
    return ESP_OK;
}

void led_driver_show(LedColor color)
{
    if (s_channel == NULL || s_encoder == NULL) {
        return;
    }
    /* WS2812 expects the byte order green, red, blue. */
    const uint8_t grb[3] = {color.g, color.r, color.b};
    const rmt_transmit_config_t tx_cfg = {.loop_count = 0};
    if (rmt_transmit(s_channel, s_encoder, grb, sizeof(grb), &tx_cfg) != ESP_OK) {
        return;
    }
    /* Block until the 24-bit frame is out (~30 us) so the following idle gap
     * latches it; trivial cost at the 50 Hz status cadence. */
    rmt_tx_wait_all_done(s_channel, LED_RMT_DONE_TIMEOUT_MS);
}
