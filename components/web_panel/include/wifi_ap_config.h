#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pure SoftAP configuration validation (Unit 10).
 *
 * CRITICAL SECURITY (R14): the AP must NEVER come up open. The runtime HAL
 * (wifi_ap.c) calls this guard before esp_wifi_start and fail-fasts (abort) on
 * an invalid config, so a build with an empty/too-short password or an OPEN
 * authmode can never start an open access point.
 *
 * This validator is framework-agnostic (no IDF includes, mirrors WIFI_AUTH_*
 * with its own discriminated enum) so the security intent is host-testable
 * without linking esp_wifi: it directly encodes the [HW] "assert non-OPEN"
 * intent as a compile/runtime guard provable on the host.
 */

/* WPA2-PSK requires an 8..63 character ASCII passphrase. */
#define WIFI_AP_PASSWORD_MIN_LEN 8U
#define WIFI_AP_PASSWORD_MAX_LEN 63U

/**
 * Authentication mode mirror (matches the subset of wifi_auth_mode_t we use).
 * Kept independent of esp_wifi so the validator stays pure.
 */
typedef enum {
    WIFI_AP_AUTH_OPEN = 0,     /* mirrors WIFI_AUTH_OPEN: forbidden */
    WIFI_AP_AUTH_WPA2_PSK = 1, /* mirrors WIFI_AUTH_WPA2_PSK: required */
} wifi_ap_authmode;

/**
 * Whether an AP config is safe to start (pure).
 *
 * Valid only when authmode is WPA2-PSK AND the password length is within the
 * WPA2 passphrase bounds (8..63). OPEN, NULL, empty, or out-of-range passwords
 * are invalid.
 *
 * @param authmode      The resolved AP auth mode.
 * @param password      The configured passphrase (may be NULL).
 * @return true only for a non-open, properly-keyed config.
 */
bool wifi_ap_config_valid(wifi_ap_authmode authmode, const char *password);

#ifdef __cplusplus
}
#endif
