#include <stdint.h>
#include <string.h>

#include "blob_codec.h"
#include "settings_model.h"
#include "settings_validate.h" /* settings_load_defaults */
#include "unity.h"

/* ---- CRC32 known-answer (anchors the polynomial/init/final choice) ---- */

static void test_crc32_check_value_is_standard(void)
{
    /* The canonical CRC32 "check" value: CRC of ASCII "123456789" under the
     * IEEE 802.3 / zlib parameters is 0xCBF43926. */
    const uint8_t input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

    uint32_t crc = blob_codec_crc32(input, sizeof(input));

    TEST_ASSERT_EQUAL_HEX32(0xCBF43926U, crc);
}

static void test_crc32_empty_range_is_zero(void)
{
    /* CRC32 of the empty range is 0 (init ^ final with no bytes). */
    uint32_t crc = blob_codec_crc32(NULL, 0);

    TEST_ASSERT_EQUAL_HEX32(0x00000000U, crc);
}

/* ---- round-trip ---- */

static settings_params make_sample(void)
{
    settings_params p;
    settings_load_defaults(&p);
    /* Perturb a few fields away from defaults to make the round-trip meaningful. */
    p.rc_min_us = 1010;
    p.rc_mid_us = 1505;
    p.rc_max_us = 1990;
    p.servo_reverse = true;
    p.throttle_reverse = true;
    p.max_throttle_fwd_pct = 85;
    p.max_throttle_rev_pct = 25;
    p.esc_neutral_us = 1480;
    p.ch4_mode_switch_enabled = false; /* non-default to make the round-trip real */
    p.ch4_switch_threshold_us = 1820;
    p.deploy_servo_us = 2100;
    p.click_window_ms = 600;
    p.servo_trim_us = -150; /* SIGNED: exercise a negative trim round-trip */
    p.spot_lock_deadband_m = 5;
    p.spot_lock_max_throttle_pct = 40;
    p.spot_lock_throttle_gain = 25;
    p.spot_lock_servo_gain = 15;
    return p;
}

static void test_round_trip_preserves_every_field(void)
{
    /* Arrange */
    settings_params in = make_sample();
    uint8_t blob[BLOB_CODEC_SIZE];

    /* Act */
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_OK,
                          blob_codec_encode(&in, blob, sizeof(blob)));
    settings_params out;
    blob_codec_result decoded = blob_codec_decode(blob, sizeof(blob), &out);

    /* Assert: accepted and every field equal (schema_version stamped). Compared
     * field-by-field, not memcmp, so struct padding cannot cause a flake. */
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_OK, decoded);
    TEST_ASSERT_EQUAL_UINT16(SETTINGS_SCHEMA_VERSION, out.schema_version);
    TEST_ASSERT_EQUAL_UINT16(in.rc_min_us, out.rc_min_us);
    TEST_ASSERT_EQUAL_UINT16(in.rc_mid_us, out.rc_mid_us);
    TEST_ASSERT_EQUAL_UINT16(in.rc_max_us, out.rc_max_us);
    TEST_ASSERT_EQUAL_UINT16(in.servo_slew_us_per_cycle,
                             out.servo_slew_us_per_cycle);
    TEST_ASSERT_EQUAL_UINT16(in.servo_min_us, out.servo_min_us);
    TEST_ASSERT_EQUAL_UINT16(in.servo_max_us, out.servo_max_us);
    TEST_ASSERT_EQUAL_UINT16(in.steer_deadband_us, out.steer_deadband_us);
    TEST_ASSERT_EQUAL_INT(in.servo_reverse, out.servo_reverse);
    TEST_ASSERT_EQUAL_UINT16(in.esc_ramp_up_us_per_cycle,
                             out.esc_ramp_up_us_per_cycle);
    TEST_ASSERT_EQUAL_UINT16(in.esc_ramp_down_us_per_cycle,
                             out.esc_ramp_down_us_per_cycle);
    TEST_ASSERT_EQUAL_UINT16(in.throttle_deadband_us, out.throttle_deadband_us);
    TEST_ASSERT_EQUAL_UINT16(in.max_throttle_fwd_pct, out.max_throttle_fwd_pct);
    TEST_ASSERT_EQUAL_UINT16(in.max_throttle_rev_pct, out.max_throttle_rev_pct);
    TEST_ASSERT_EQUAL_INT(in.throttle_reverse, out.throttle_reverse);
    TEST_ASSERT_EQUAL_UINT16(in.esc_neutral_us, out.esc_neutral_us);
    TEST_ASSERT_EQUAL_UINT16(in.esc_neutral_band_us, out.esc_neutral_band_us);
    TEST_ASSERT_EQUAL_UINT16(in.esc_forward_min_us, out.esc_forward_min_us);
    TEST_ASSERT_EQUAL_UINT16(in.esc_forward_max_us, out.esc_forward_max_us);
    TEST_ASSERT_EQUAL_UINT16(in.esc_reverse_min_us, out.esc_reverse_min_us);
    TEST_ASSERT_EQUAL_UINT16(in.esc_reverse_max_us, out.esc_reverse_max_us);
    TEST_ASSERT_EQUAL_UINT16(in.failsafe_timeout_ms, out.failsafe_timeout_ms);
    TEST_ASSERT_EQUAL_UINT16(in.reverse_neutral_dwell_ms,
                             out.reverse_neutral_dwell_ms);
    TEST_ASSERT_EQUAL_INT(in.ch4_mode_switch_enabled,
                          out.ch4_mode_switch_enabled);
    TEST_ASSERT_EQUAL_UINT16(in.ch4_switch_threshold_us,
                             out.ch4_switch_threshold_us);
    TEST_ASSERT_EQUAL_UINT16(in.deploy_servo_us, out.deploy_servo_us);
    TEST_ASSERT_EQUAL_UINT16(in.click_window_ms, out.click_window_ms);
    TEST_ASSERT_EQUAL_INT16(in.servo_trim_us, out.servo_trim_us);
    TEST_ASSERT_EQUAL_UINT16(in.spot_lock_deadband_m, out.spot_lock_deadband_m);
    TEST_ASSERT_EQUAL_UINT16(in.spot_lock_max_throttle_pct,
                             out.spot_lock_max_throttle_pct);
    TEST_ASSERT_EQUAL_UINT16(in.spot_lock_throttle_gain,
                             out.spot_lock_throttle_gain);
    TEST_ASSERT_EQUAL_UINT16(in.spot_lock_servo_gain, out.spot_lock_servo_gain);
}

static void test_negative_servo_trim_round_trips(void)
{
    /* Arrange: a negative signed trim must survive the LE-int16 bit pattern. */
    settings_params in = make_sample();
    in.servo_trim_us = -300; /* the extreme negative trim */
    uint8_t blob[BLOB_CODEC_SIZE];

    /* Act */
    blob_codec_encode(&in, blob, sizeof(blob));
    settings_params out;
    blob_codec_result decoded = blob_codec_decode(blob, sizeof(blob), &out);

    /* Assert: decoded as the same negative value (not a large unsigned). */
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_OK, decoded);
    TEST_ASSERT_EQUAL_INT16(-300, out.servo_trim_us);
}

static void test_blob_size_matches_v6_layout(void)
{
    /* Anchor the schema-v6 wire size: the schema_version + every field (LE) + 4
     * CRC bytes. v6 added the four spot-lock regulator params (4x u16, +8 over
     * v5's 53 field bytes -> 61; total 65). A struct/layout change that forgets
     * to update the codec size trips this. */
    TEST_ASSERT_EQUAL_UINT(61U, BLOB_CODEC_FIELD_BYTES);
    TEST_ASSERT_EQUAL_UINT(65U, BLOB_CODEC_SIZE);
}

static void test_encode_stamps_current_schema_version(void)
{
    /* Arrange: caller-supplied schema_version is ignored; stored is current. */
    settings_params in = make_sample();
    in.schema_version = 0xBEEF;
    uint8_t blob[BLOB_CODEC_SIZE];

    /* Act */
    blob_codec_encode(&in, blob, sizeof(blob));
    settings_params out;
    blob_codec_decode(blob, sizeof(blob), &out);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT16(SETTINGS_SCHEMA_VERSION, out.schema_version);
}

/* ---- rejection paths ---- */

static void test_bad_crc_is_rejected(void)
{
    /* Arrange: flip a payload byte so the stored CRC no longer matches. */
    settings_params in = make_sample();
    uint8_t blob[BLOB_CODEC_SIZE];
    blob_codec_encode(&in, blob, sizeof(blob));
    blob[5] ^= 0xFFU;

    /* Act */
    settings_params out;
    blob_codec_result decoded = blob_codec_decode(blob, sizeof(blob), &out);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_ERR_CRC, decoded);
}

static void test_corrupt_crc_trailer_is_rejected(void)
{
    /* Arrange: corrupt the CRC trailer itself (payload intact). */
    settings_params in = make_sample();
    uint8_t blob[BLOB_CODEC_SIZE];
    blob_codec_encode(&in, blob, sizeof(blob));
    blob[BLOB_CODEC_SIZE - 1] ^= 0x01U;

    /* Act */
    settings_params out;
    blob_codec_result decoded = blob_codec_decode(blob, sizeof(blob), &out);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_ERR_CRC, decoded);
}

static void test_wrong_length_too_short_is_rejected(void)
{
    /* Arrange: a one-byte-short buffer. */
    settings_params in = make_sample();
    uint8_t blob[BLOB_CODEC_SIZE];
    blob_codec_encode(&in, blob, sizeof(blob));

    /* Act */
    settings_params out;
    blob_codec_result decoded =
        blob_codec_decode(blob, BLOB_CODEC_SIZE - 1U, &out);

    /* Assert: length checked before CRC. */
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_ERR_LENGTH, decoded);
}

static void test_wrong_length_too_long_is_rejected(void)
{
    /* Arrange */
    settings_params in = make_sample();
    uint8_t blob[BLOB_CODEC_SIZE + 1U];
    blob_codec_encode(&in, blob, BLOB_CODEC_SIZE);

    /* Act */
    settings_params out;
    blob_codec_result decoded =
        blob_codec_decode(blob, BLOB_CODEC_SIZE + 1U, &out);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_ERR_LENGTH, decoded);
}

static void test_other_schema_version_is_rejected(void)
{
    /* Arrange: encode, then rewrite schema_version (bytes 0..1) and re-stamp the
     * CRC so the only thing wrong is the schema. */
    settings_params in = make_sample();
    uint8_t blob[BLOB_CODEC_SIZE];
    blob_codec_encode(&in, blob, sizeof(blob));
    blob[0] = (uint8_t)((SETTINGS_SCHEMA_VERSION + 1U) & 0xFFU);
    blob[1] = (uint8_t)(((SETTINGS_SCHEMA_VERSION + 1U) >> 8) & 0xFFU);
    uint32_t crc = blob_codec_crc32(blob, BLOB_CODEC_FIELD_BYTES);
    blob[BLOB_CODEC_FIELD_BYTES + 0] = (uint8_t)(crc & 0xFFU);
    blob[BLOB_CODEC_FIELD_BYTES + 1] = (uint8_t)((crc >> 8) & 0xFFU);
    blob[BLOB_CODEC_FIELD_BYTES + 2] = (uint8_t)((crc >> 16) & 0xFFU);
    blob[BLOB_CODEC_FIELD_BYTES + 3] = (uint8_t)((crc >> 24) & 0xFFU);

    /* Act: CRC passes (re-stamped), so the schema check is what rejects it. */
    settings_params out;
    blob_codec_result decoded = blob_codec_decode(blob, sizeof(blob), &out);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_ERR_SCHEMA, decoded);
}

static void test_prior_schema_v5_is_rejected(void)
{
    /* Arrange: a v6-sized blob carrying the prior schema 5 (the pre-spot-lock
     * layout) with a valid CRC must be rejected: settings reload defaults rather
     * than silently mis-read the new spot-lock fields from old bytes. */
    settings_params in = make_sample();
    uint8_t blob[BLOB_CODEC_SIZE];
    blob_codec_encode(&in, blob, sizeof(blob));
    blob[0] = 5U; /* schema_version low byte = 5 */
    blob[1] = 0U;
    uint32_t crc = blob_codec_crc32(blob, BLOB_CODEC_FIELD_BYTES);
    blob[BLOB_CODEC_FIELD_BYTES + 0] = (uint8_t)(crc & 0xFFU);
    blob[BLOB_CODEC_FIELD_BYTES + 1] = (uint8_t)((crc >> 8) & 0xFFU);
    blob[BLOB_CODEC_FIELD_BYTES + 2] = (uint8_t)((crc >> 16) & 0xFFU);
    blob[BLOB_CODEC_FIELD_BYTES + 3] = (uint8_t)((crc >> 24) & 0xFFU);

    /* Act */
    settings_params out;
    blob_codec_result decoded = blob_codec_decode(blob, sizeof(blob), &out);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_ERR_SCHEMA, decoded);
}

static void test_null_args_are_rejected(void)
{
    settings_params in = make_sample();
    uint8_t blob[BLOB_CODEC_SIZE];
    settings_params out;

    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_ERR_ARG,
                          blob_codec_encode(NULL, blob, sizeof(blob)));
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_ERR_ARG,
                          blob_codec_encode(&in, NULL, sizeof(blob)));
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_ERR_ARG,
                          blob_codec_decode(NULL, BLOB_CODEC_SIZE, &out));
    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_ERR_ARG,
                          blob_codec_decode(blob, BLOB_CODEC_SIZE, NULL));
}

static void test_encode_into_too_small_buffer_is_rejected(void)
{
    settings_params in = make_sample();
    uint8_t blob[BLOB_CODEC_SIZE];

    blob_codec_result encoded =
        blob_codec_encode(&in, blob, BLOB_CODEC_SIZE - 1U);

    TEST_ASSERT_EQUAL_INT(BLOB_CODEC_ERR_LENGTH, encoded);
}

void run_blob_codec_tests(void)
{
    RUN_TEST(test_crc32_check_value_is_standard);
    RUN_TEST(test_crc32_empty_range_is_zero);
    RUN_TEST(test_round_trip_preserves_every_field);
    RUN_TEST(test_negative_servo_trim_round_trips);
    RUN_TEST(test_blob_size_matches_v6_layout);
    RUN_TEST(test_encode_stamps_current_schema_version);
    RUN_TEST(test_bad_crc_is_rejected);
    RUN_TEST(test_corrupt_crc_trailer_is_rejected);
    RUN_TEST(test_wrong_length_too_short_is_rejected);
    RUN_TEST(test_wrong_length_too_long_is_rejected);
    RUN_TEST(test_other_schema_version_is_rejected);
    RUN_TEST(test_prior_schema_v5_is_rejected);
    RUN_TEST(test_null_args_are_rejected);
    RUN_TEST(test_encode_into_too_small_buffer_is_rejected);
}
