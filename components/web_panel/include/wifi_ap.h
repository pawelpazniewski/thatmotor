#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * WPA2-PSK SoftAP bring-up (Unit 10), thin esp_wifi HAL.
 *
 * Starts the on-board access point in WPA2-PSK mode on a fixed channel with a
 * small max-connection cap. The configured password (Kconfig) is validated by
 * the pure wifi_ap_config_valid guard BEFORE esp_wifi_start; an OPEN or
 * empty/too-short password is a fatal misconfiguration (abort/fail-fast) so the
 * firmware can NEVER start an open access point (R14).
 */

/**
 * Bring up the WPA2-PSK SoftAP. Fail-fasts (abort) if the resolved config would
 * not be WPA2-PSK with a valid passphrase. Initialises NETIF + the default
 * event loop + Wi-Fi as needed.
 *
 * @return ESP_OK on success, otherwise the failing esp_err_t from setup.
 */
esp_err_t wifi_ap_start(void);

#ifdef __cplusplus
}
#endif
