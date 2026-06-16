#include "unity.h"
#include "wifi_ap_config.h"

/* --- OPEN / empty -> invalid (security: never start an open AP) --- */

static void test_open_authmode_is_invalid(void)
{
    /* Act + Assert: OPEN is rejected even with a non-empty password. */
    TEST_ASSERT_FALSE(wifi_ap_config_valid(WIFI_AP_AUTH_OPEN, "longenough"));
}

static void test_null_password_is_invalid(void)
{
    /* Act + Assert */
    TEST_ASSERT_FALSE(wifi_ap_config_valid(WIFI_AP_AUTH_WPA2_PSK, NULL));
}

static void test_empty_password_is_invalid(void)
{
    /* Act + Assert */
    TEST_ASSERT_FALSE(wifi_ap_config_valid(WIFI_AP_AUTH_WPA2_PSK, ""));
}

static void test_too_short_password_is_invalid(void)
{
    /* Arrange: 7 chars, one below the WPA2 minimum of 8. */
    /* Act + Assert */
    TEST_ASSERT_FALSE(wifi_ap_config_valid(WIFI_AP_AUTH_WPA2_PSK, "1234567"));
}

static void test_too_long_password_is_invalid(void)
{
    /* Arrange: 64 chars, one above the WPA2 maximum of 63. */
    char pw[65];
    for (int i = 0; i < 64; ++i) {
        pw[i] = 'a';
    }
    pw[64] = '\0';

    /* Act + Assert */
    TEST_ASSERT_FALSE(wifi_ap_config_valid(WIFI_AP_AUTH_WPA2_PSK, pw));
}

/* --- WPA2 + valid password -> valid --- */

static void test_wpa2_with_min_length_password_is_valid(void)
{
    /* Arrange: exactly 8 chars (inclusive lower boundary). */
    /* Act + Assert */
    TEST_ASSERT_TRUE(wifi_ap_config_valid(WIFI_AP_AUTH_WPA2_PSK, "12345678"));
}

static void test_wpa2_with_typical_password_is_valid(void)
{
    /* Act + Assert */
    TEST_ASSERT_TRUE(wifi_ap_config_valid(WIFI_AP_AUTH_WPA2_PSK, "kayak-motor-ap"));
}

static void test_wpa2_with_max_length_password_is_valid(void)
{
    /* Arrange: exactly 63 chars (inclusive upper boundary). */
    char pw[64];
    for (int i = 0; i < 63; ++i) {
        pw[i] = 'a';
    }
    pw[63] = '\0';

    /* Act + Assert */
    TEST_ASSERT_TRUE(wifi_ap_config_valid(WIFI_AP_AUTH_WPA2_PSK, pw));
}

void run_wifi_ap_config_tests(void)
{
    RUN_TEST(test_open_authmode_is_invalid);
    RUN_TEST(test_null_password_is_invalid);
    RUN_TEST(test_empty_password_is_invalid);
    RUN_TEST(test_too_short_password_is_invalid);
    RUN_TEST(test_too_long_password_is_invalid);
    RUN_TEST(test_wpa2_with_min_length_password_is_valid);
    RUN_TEST(test_wpa2_with_typical_password_is_valid);
    RUN_TEST(test_wpa2_with_max_length_password_is_valid);
}
