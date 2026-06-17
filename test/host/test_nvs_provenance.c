#include <stdbool.h>
#include <stdint.h>

#include "blob_codec.h"
#include "nvs_provenance.h"
#include "settings_model.h"
#include "settings_ranges.h"
#include "settings_validate.h"
#include "unity.h"

/* A blob that round-trips through resolve_provenance unchanged must already
 * satisfy every per-field sanity range; this asserts the load-time invariant
 * that a rejected store never yields a value outside the sanity window. */
static void assert_within_sanity_window(const settings_params *p)
{
    TEST_ASSERT_TRUE(p->rc_min_us >= RC_US_MIN && p->rc_min_us <= RC_US_MAX);
    TEST_ASSERT_TRUE(p->rc_mid_us >= RC_US_MIN && p->rc_mid_us <= RC_US_MAX);
    TEST_ASSERT_TRUE(p->rc_max_us >= RC_US_MIN && p->rc_max_us <= RC_US_MAX);
    TEST_ASSERT_TRUE(p->rc_min_us < p->rc_mid_us && p->rc_mid_us < p->rc_max_us);
    TEST_ASSERT_TRUE(p->esc_neutral_us >= ESC_NEUTRAL_MIN &&
                     p->esc_neutral_us <= ESC_NEUTRAL_MAX);
    TEST_ASSERT_TRUE(p->max_throttle_fwd_pct >= MAX_THROTTLE_PCT_MIN &&
                     p->max_throttle_fwd_pct <= MAX_THROTTLE_PCT_MAX);
    TEST_ASSERT_TRUE(p->max_throttle_rev_pct >= MAX_THROTTLE_PCT_MIN &&
                     p->max_throttle_rev_pct <= MAX_THROTTLE_PCT_MAX);
    TEST_ASSERT_TRUE(p->failsafe_timeout_ms >= FAILSAFE_TIMEOUT_MS_MIN &&
                     p->failsafe_timeout_ms <= FAILSAFE_TIMEOUT_MS_MAX);
}

/* A valid stored blob (decodes clean and every field already in range). */
static settings_params make_valid_stored(void)
{
    settings_params p;
    settings_load_defaults(&p);
    /* Perturb a few in-range fields so source==NVS is meaningful (not defaults). */
    p.rc_min_us = 1010;
    p.rc_mid_us = 1500;
    p.rc_max_us = 1990;
    p.max_throttle_fwd_pct = 85;
    p.max_throttle_rev_pct = 42;
    return p;
}

/* ---- empty store ---- */

static void test_not_found_yields_defaults_without_error(void)
{
    /* Arrange / Act: no blob present. decoded/decode are irrelevant. */
    settings_params out;
    settings_validation_result result =
        resolve_provenance(NVS_READ_NOT_FOUND, BLOB_CODEC_ERR_ARG, NULL, &out);

    /* Assert: silent conservative defaults, no nvs_error. */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_DEFAULTS, result.source);
    TEST_ASSERT_TRUE(result.defaults_used);
    TEST_ASSERT_FALSE(result.nvs_error);
    TEST_ASSERT_FALSE(result.calibrated);
    assert_within_sanity_window(&out);
}

/* ---- corruption: CRC ---- */

static void test_bad_crc_yields_defaults_with_error(void)
{
    /* Arrange: blob present but decode failed the CRC check. */
    settings_params out;

    /* Act */
    settings_validation_result result =
        resolve_provenance(NVS_READ_OK, BLOB_CODEC_ERR_CRC, NULL, &out);

    /* Assert: corruption -> defaults + nvs_error, still inside sanity window. */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_DEFAULTS, result.source);
    TEST_ASSERT_TRUE(result.defaults_used);
    TEST_ASSERT_TRUE(result.nvs_error);
    assert_within_sanity_window(&out);
}

/* ---- corruption: length ---- */

static void test_bad_length_yields_defaults_with_error(void)
{
    settings_params out;

    settings_validation_result result =
        resolve_provenance(NVS_READ_OK, BLOB_CODEC_ERR_LENGTH, NULL, &out);

    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_DEFAULTS, result.source);
    TEST_ASSERT_TRUE(result.nvs_error);
    assert_within_sanity_window(&out);
}

/* ---- alert: newer schema (not corruption) ---- */

static void test_other_schema_yields_defaults_as_alert_not_error(void)
{
    /* Arrange: a newer schema version is an alert, not corruption. */
    settings_params out;

    /* Act */
    settings_validation_result result =
        resolve_provenance(NVS_READ_OK, BLOB_CODEC_ERR_SCHEMA, NULL, &out);

    /* Assert: defaults loaded but nvs_error stays false (alert != corruption). */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_DEFAULTS, result.source);
    TEST_ASSERT_TRUE(result.defaults_used);
    TEST_ASSERT_FALSE(result.nvs_error);
    assert_within_sanity_window(&out);
}

/* ---- valid blob ---- */

static void test_valid_blob_yields_nvs_source_and_keeps_params(void)
{
    /* Arrange: a clean, in-range stored blob. */
    settings_params stored = make_valid_stored();
    settings_params out;

    /* Act */
    settings_validation_result result =
        resolve_provenance(NVS_READ_OK, BLOB_CODEC_OK, &stored, &out);

    /* Assert: source NVS, validated true, params preserved, no error/defaults. */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_NVS, result.source);
    TEST_ASSERT_TRUE(result.settings_valid);
    TEST_ASSERT_TRUE(result.calibrated);
    TEST_ASSERT_FALSE(result.defaults_used);
    TEST_ASSERT_FALSE(result.nvs_error);
    TEST_ASSERT_EQUAL_UINT16(stored.rc_min_us, out.rc_min_us);
    TEST_ASSERT_EQUAL_UINT16(stored.rc_max_us, out.rc_max_us);
    TEST_ASSERT_EQUAL_UINT16(stored.max_throttle_fwd_pct,
                             out.max_throttle_fwd_pct);
    TEST_ASSERT_EQUAL_UINT16(stored.max_throttle_rev_pct,
                             out.max_throttle_rev_pct);
}

/* ---- valid-but-out-of-range blob: Unit 4 recovery, no nvs_error ---- */

static void test_decoded_out_of_range_yields_mixed_recovered_no_error(void)
{
    /* Arrange: blob decoded OK but one field is out of its sanity range, so the
     * Unit 4 validator must repair it (MIXED_RECOVERED), and this is NOT an
     * nvs read error (the blob was perfectly readable). */
    settings_params stored = make_valid_stored();
    stored.max_throttle_fwd_pct = MAX_THROTTLE_PCT_MAX + 50U; /* out of range */
    settings_params out;

    /* Act */
    settings_validation_result result =
        resolve_provenance(NVS_READ_OK, BLOB_CODEC_OK, &stored, &out);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_MIXED_RECOVERED, result.source);
    TEST_ASSERT_TRUE(result.defaults_used);
    TEST_ASSERT_FALSE(result.settings_valid);
    TEST_ASSERT_FALSE(result.nvs_error);
    assert_within_sanity_window(&out);
}

/* ---- read error (blob present but unreadable) ---- */

static void test_read_error_yields_defaults_with_error(void)
{
    settings_params out;

    settings_validation_result result =
        resolve_provenance(NVS_READ_ERROR, BLOB_CODEC_ERR_ARG, NULL, &out);

    TEST_ASSERT_EQUAL_INT(SETTINGS_SOURCE_DEFAULTS, result.source);
    TEST_ASSERT_TRUE(result.nvs_error);
    assert_within_sanity_window(&out);
}

void run_nvs_provenance_tests(void)
{
    RUN_TEST(test_not_found_yields_defaults_without_error);
    RUN_TEST(test_bad_crc_yields_defaults_with_error);
    RUN_TEST(test_bad_length_yields_defaults_with_error);
    RUN_TEST(test_other_schema_yields_defaults_as_alert_not_error);
    RUN_TEST(test_valid_blob_yields_nvs_source_and_keeps_params);
    RUN_TEST(test_decoded_out_of_range_yields_mixed_recovered_no_error);
    RUN_TEST(test_read_error_yields_defaults_with_error);
}
