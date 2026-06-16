#include "wifi_ap_config.h"

#include <string.h>

bool wifi_ap_config_valid(wifi_ap_authmode authmode, const char *password)
{
    if (authmode != WIFI_AP_AUTH_WPA2_PSK) {
        return false;
    }
    if (password == NULL) {
        return false;
    }
    size_t len = strlen(password);
    if (len < WIFI_AP_PASSWORD_MIN_LEN || len > WIFI_AP_PASSWORD_MAX_LEN) {
        return false;
    }
    return true;
}
