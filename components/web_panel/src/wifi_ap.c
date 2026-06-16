#include "wifi_ap.h"

#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "wifi_ap_config.h"

static const char *TAG = "wifi_ap";

/* The AP authmode is fixed at WPA2-PSK; the security guard below proves it is
 * never weakened to OPEN. The password is a Kconfig placeholder (NOT a real
 * secret): empty/too-short -> fail-fast at startup. */
#define AP_AUTHMODE WIFI_AUTH_WPA2_PSK

/* Initialise the default NVS partition (Wi-Fi calibration store). Tolerates a
 * full/version-mismatched partition by erasing and retrying once. */
static esp_err_t init_default_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

/* Fail-fast SECURITY guard (R14): refuse to start unless the resolved config is
 * WPA2-PSK with a valid passphrase. An OPEN/empty/too-short config aborts here
 * so an open access point can never come up. */
static void assert_ap_secure(const char *password)
{
    wifi_ap_authmode mode = (AP_AUTHMODE == WIFI_AUTH_OPEN)
                                ? WIFI_AP_AUTH_OPEN
                                : WIFI_AP_AUTH_WPA2_PSK;
    if (wifi_ap_config_valid(mode, password)) {
        return;
    }
    ESP_LOGE(TAG, "refusing to start AP: not WPA2-PSK or invalid password "
                  "(set CONFIG_KAYAK_AP_PASSWORD to 8..63 chars)");
    abort();
}

/* Populate the SoftAP wifi_config from Kconfig, post-guard. */
static wifi_config_t build_ap_config(void)
{
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.ap.ssid, CONFIG_KAYAK_AP_SSID, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = (uint8_t)strlen(CONFIG_KAYAK_AP_SSID);
    strlcpy((char *)cfg.ap.password, CONFIG_KAYAK_AP_PASSWORD,
            sizeof(cfg.ap.password));
    cfg.ap.channel = CONFIG_KAYAK_AP_CHANNEL;
    cfg.ap.max_connection = CONFIG_KAYAK_AP_MAX_CONN;
    cfg.ap.authmode = AP_AUTHMODE;
    return cfg;
}

esp_err_t wifi_ap_start(void)
{
    /* SECURITY: validate the resolved config BEFORE touching the radio. */
    assert_ap_secure(CONFIG_KAYAK_AP_PASSWORD);

    esp_err_t err = init_default_nvs();
    if (err != ESP_OK) {
        return err;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    wifi_config_t ap_cfg = build_ap_config();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP up: ssid=%s channel=%d max_conn=%d (WPA2-PSK)",
             CONFIG_KAYAK_AP_SSID, CONFIG_KAYAK_AP_CHANNEL,
             CONFIG_KAYAK_AP_MAX_CONN);
    return ESP_OK;
}
