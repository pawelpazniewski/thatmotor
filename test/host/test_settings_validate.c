#include "settings_model.h"
#include "settings_validate.h"
#include "unity.h"

static void test_empty_nvs_yields_defaults(void)
{
    /* Arrange: no stored blob. */
    settings_params out;

    /* Act */
    settings_validation_result result = settings_validate(NULL, false, &out);

    /* Assert: pure defaults, marked uncalibrated. */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_DEFAULTS, result.source);
    TEST_ASSERT_TRUE(result.defaults_used);
    TEST_ASSERT_FALSE(result.calibrated);
    TEST_ASSERT_FALSE(result.settings_valid);
    TEST_ASSERT_EQUAL_UINT16(1500U, out.esc_neutral_us);
}

static void test_defaults_pass_their_own_validation(void)
{
    /* Arrange: feed the defaults back through validation as a stored blob. */
    settings_params defaults;
    settings_load_defaults(&defaults);
    settings_params out;

    /* Act */
    settings_validation_result result =
        settings_validate(&defaults, true, &out);

    /* Assert: defaults are internally consistent -> clean NVS load. */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_NVS, result.source);
    TEST_ASSERT_TRUE(result.settings_valid);
    TEST_ASSERT_TRUE(result.calibrated);
    TEST_ASSERT_FALSE(result.defaults_used);
}

static void test_valid_stored_blob_is_nvs_valid(void)
{
    /* Arrange: a hand-tuned but valid blob. */
    settings_params stored;
    settings_load_defaults(&stored);
    stored.max_throttle_fwd_pct = 80U; /* still within [0,100] */
    stored.max_throttle_rev_pct = 40U;
    stored.servo_slew_us_per_cycle = 20U;
    settings_params out;

    /* Act */
    settings_validation_result result = settings_validate(&stored, true, &out);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_NVS, result.source);
    TEST_ASSERT_TRUE(result.settings_valid);
    TEST_ASSERT_EQUAL_UINT16(80U, out.max_throttle_fwd_pct);
    TEST_ASSERT_EQUAL_UINT16(40U, out.max_throttle_rev_pct);
}

static void test_forward_throttle_out_of_range_recovers(void)
{
    /* Arrange: forward limit out of range; everything else valid. */
    settings_params stored;
    settings_load_defaults(&stored);
    stored.max_throttle_fwd_pct = 250U; /* > 100, invalid */
    settings_params out;

    /* Act */
    settings_validation_result result = settings_validate(&stored, true, &out);

    /* Assert: mixed recovery; bad field falls back to its default, rest kept. */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_MIXED_RECOVERED, result.source);
    TEST_ASSERT_TRUE(result.defaults_used);
    TEST_ASSERT_FALSE(result.settings_valid);
    TEST_ASSERT_FALSE(result.calibrated);
    TEST_ASSERT_EQUAL_UINT16(90U, out.max_throttle_fwd_pct); /* fwd default */
    TEST_ASSERT_EQUAL_UINT16(50U, out.max_throttle_rev_pct); /* rev kept */
}

static void test_reverse_throttle_out_of_range_recovers(void)
{
    /* Arrange: reverse limit out of range; forward valid and preserved. */
    settings_params stored;
    settings_load_defaults(&stored);
    stored.max_throttle_fwd_pct = 75U;  /* valid, must survive */
    stored.max_throttle_rev_pct = 200U; /* > 100, invalid */
    settings_params out;

    /* Act */
    settings_validation_result result = settings_validate(&stored, true, &out);

    /* Assert: only the reverse field falls back to its default. */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_MIXED_RECOVERED, result.source);
    TEST_ASSERT_TRUE(result.defaults_used);
    TEST_ASSERT_FALSE(result.settings_valid);
    TEST_ASSERT_EQUAL_UINT16(75U, out.max_throttle_fwd_pct); /* fwd kept */
    TEST_ASSERT_EQUAL_UINT16(50U, out.max_throttle_rev_pct); /* rev default */
}

static void test_cross_field_esc_forward_inverted_rejected(void)
{
    /* Arrange: escForwardMin > escForwardMax violates the band invariant. */
    settings_params stored;
    settings_load_defaults(&stored);
    stored.esc_forward_min_us = 1900U;
    stored.esc_forward_max_us = 1600U;
    settings_params out;

    /* Act */
    settings_validation_result result = settings_validate(&stored, true, &out);

    /* Assert: cross-field repair -> mixed recovered, band restored ascending. */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_MIXED_RECOVERED, result.source);
    TEST_ASSERT_TRUE(result.defaults_used);
    TEST_ASSERT_TRUE(out.esc_forward_min_us <= out.esc_forward_max_us);
}

static void test_cross_field_rc_non_monotonic_rejected(void)
{
    /* Arrange: rc_mid below rc_min breaks monotonic min<mid<max. */
    settings_params stored;
    settings_load_defaults(&stored);
    stored.rc_min_us = 1600U;
    stored.rc_mid_us = 1500U;
    stored.rc_max_us = 2000U;
    settings_params out;

    /* Act */
    settings_validation_result result = settings_validate(&stored, true, &out);

    /* Assert: RC calibration restored to monotonic defaults. */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_MIXED_RECOVERED, result.source);
    TEST_ASSERT_TRUE(out.rc_min_us < out.rc_mid_us);
    TEST_ASSERT_TRUE(out.rc_mid_us < out.rc_max_us);
}

static void test_cross_field_esc_map_inverted_rejected(void)
{
    /* Arrange: per-field-valid but the ESC map endpoints are non-monotonic.
     * neutral 1600 (max allowed) sits ABOVE forward_max 1550, so the map would
     * scale the forward half with a negative span (inverted direction). Keep the
     * forward/reverse band checks satisfied so only the new invariant fires. */
    settings_params stored;
    settings_load_defaults(&stored);
    stored.esc_neutral_us = 1600U;
    stored.esc_forward_min_us = 1550U;
    stored.esc_forward_max_us = 1550U;
    settings_params out;

    /* Act */
    settings_validation_result result = settings_validate(&stored, true, &out);

    /* Assert: repaired to mixed-recovered and the map is monotonic again. */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_MIXED_RECOVERED, result.source);
    TEST_ASSERT_TRUE(result.defaults_used);
    TEST_ASSERT_TRUE(out.esc_reverse_max_us < out.esc_neutral_us);
    TEST_ASSERT_TRUE(out.esc_neutral_us < out.esc_forward_max_us);
}

void run_settings_validate_tests(void)
{
    RUN_TEST(test_empty_nvs_yields_defaults);
    RUN_TEST(test_defaults_pass_their_own_validation);
    RUN_TEST(test_valid_stored_blob_is_nvs_valid);
    RUN_TEST(test_forward_throttle_out_of_range_recovers);
    RUN_TEST(test_reverse_throttle_out_of_range_recovers);
    RUN_TEST(test_cross_field_esc_forward_inverted_rejected);
    RUN_TEST(test_cross_field_rc_non_monotonic_rejected);
    RUN_TEST(test_cross_field_esc_map_inverted_rejected);
}
