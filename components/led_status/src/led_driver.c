#include "led_driver.h"

#include "driver/gpio.h"

esp_err_t led_driver_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << LED_DRIVER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_set_level(LED_DRIVER_GPIO, 0);
}

void led_driver_set(bool on)
{
    gpio_set_level(LED_DRIVER_GPIO, on ? 1 : 0);
}
