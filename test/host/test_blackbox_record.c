#include <stdint.h>
#include <string.h>

#include "blackbox_record.h"
#include "blackbox_region.h"
#include "unity.h"

/* ---- CRC32 known-answer (anchors the polynomial/init/final choice) ---- */

static void test_crc32_check_value_is_standard(void)
{
    /* The canonical CRC32 "check" value of ASCII "123456789" under the IEEE
     * 802.3 / zlib parameters is 0xCBF43926. */
    const uint8_t input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

    uint32_t crc = blackbox_record_crc32(input, sizeof(input));

    TEST_ASSERT_EQUAL_HEX32(0xCBF43926U, crc);
}

/* ---- sample fixtures ---- */

static blackbox_sample make_sample(void)
{
    blackbox_sample s;
    s.t_ms = 1234567U;
    s.substate = 1U; /* active */
    s.err_m = 42U;
    s.bearing_deg10 = 1800U;
    s.heading_deg10 = 2705U;
    s.servo_us = 1620U;
    s.esc_us = 1555U;
    s.ch1_us = 1490U;
    s.ch2_us = 1510U;
    s.lat_e7 = -524987654; /* negative (southern) to exercise signed round-trip */
    s.lon_e7 = 213456789;
    s.sats = 11U;
    s.speed_cms = 275U;
    s.gps_fix = true;
    s.imu_ok = true;
    return s;
}

static blackbox_session_header make_header(void)
{
    blackbox_session_header h;
    h.session_seq = 7U;
    h.target_lat_e7 = -524000000;
    h.target_lon_e7 = 213000000;
    h.deadband_m = 5U;
    h.max_throttle_pct = 40U;
    h.throttle_gain = 25U;
    h.servo_gain = 15U;
    h.start_ms = 987654U;
    return h;
}

/* ---- round-trip ---- */

static void test_sample_round_trip_preserves_every_field(void)
{
    /* Arrange */
    blackbox_sample in = make_sample();
    uint8_t buf[BLACKBOX_RECORD_SIZE];

    /* Act */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_REC_OK,
        blackbox_record_encode_sample(&in, buf, sizeof(buf)));
    blackbox_sample out;
    blackbox_record_result decoded =
        blackbox_record_decode_sample(buf, sizeof(buf), &out);

    /* Assert: accepted and every field equal (field-by-field, not memcmp). */
    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_OK, decoded);
    TEST_ASSERT_EQUAL_UINT32(in.t_ms, out.t_ms);
    TEST_ASSERT_EQUAL_UINT8(in.substate, out.substate);
    TEST_ASSERT_EQUAL_UINT16(in.err_m, out.err_m);
    TEST_ASSERT_EQUAL_UINT16(in.bearing_deg10, out.bearing_deg10);
    TEST_ASSERT_EQUAL_UINT16(in.heading_deg10, out.heading_deg10);
    TEST_ASSERT_EQUAL_UINT16(in.servo_us, out.servo_us);
    TEST_ASSERT_EQUAL_UINT16(in.esc_us, out.esc_us);
    TEST_ASSERT_EQUAL_UINT16(in.ch1_us, out.ch1_us);
    TEST_ASSERT_EQUAL_UINT16(in.ch2_us, out.ch2_us);
    TEST_ASSERT_EQUAL_INT32(in.lat_e7, out.lat_e7);
    TEST_ASSERT_EQUAL_INT32(in.lon_e7, out.lon_e7);
    TEST_ASSERT_EQUAL_UINT8(in.sats, out.sats);
    TEST_ASSERT_EQUAL_UINT16(in.speed_cms, out.speed_cms);
    TEST_ASSERT_EQUAL_INT(in.gps_fix, out.gps_fix);
    TEST_ASSERT_EQUAL_INT(in.imu_ok, out.imu_ok);
}

static void test_sample_flags_false_round_trip(void)
{
    /* Arrange: both flag bits clear must decode back to false, not true. */
    blackbox_sample in = make_sample();
    in.gps_fix = false;
    in.imu_ok = false;
    uint8_t buf[BLACKBOX_RECORD_SIZE];

    /* Act */
    blackbox_record_encode_sample(&in, buf, sizeof(buf));
    blackbox_sample out;
    blackbox_record_decode_sample(buf, sizeof(buf), &out);

    /* Assert */
    TEST_ASSERT_FALSE(out.gps_fix);
    TEST_ASSERT_FALSE(out.imu_ok);
}

static void test_header_round_trip_preserves_every_field(void)
{
    /* Arrange */
    blackbox_session_header in = make_header();
    uint8_t buf[BLACKBOX_RECORD_SIZE];

    /* Act */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_REC_OK,
        blackbox_record_encode_header(&in, buf, sizeof(buf)));
    blackbox_session_header out;
    blackbox_record_result decoded =
        blackbox_record_decode_header(buf, sizeof(buf), &out);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_OK, decoded);
    TEST_ASSERT_EQUAL_UINT32(in.session_seq, out.session_seq);
    TEST_ASSERT_EQUAL_INT32(in.target_lat_e7, out.target_lat_e7);
    TEST_ASSERT_EQUAL_INT32(in.target_lon_e7, out.target_lon_e7);
    TEST_ASSERT_EQUAL_UINT16(in.deadband_m, out.deadband_m);
    TEST_ASSERT_EQUAL_UINT16(in.max_throttle_pct, out.max_throttle_pct);
    TEST_ASSERT_EQUAL_UINT16(in.throttle_gain, out.throttle_gain);
    TEST_ASSERT_EQUAL_UINT16(in.servo_gain, out.servo_gain);
    TEST_ASSERT_EQUAL_UINT32(in.start_ms, out.start_ms);
}

/* ---- classification / type discrimination ---- */

static void test_classify_reports_record_type(void)
{
    /* Arrange: a header slot and a sample slot. */
    blackbox_session_header h = make_header();
    blackbox_sample s = make_sample();
    uint8_t hbuf[BLACKBOX_RECORD_SIZE];
    uint8_t sbuf[BLACKBOX_RECORD_SIZE];
    blackbox_record_encode_header(&h, hbuf, sizeof(hbuf));
    blackbox_record_encode_sample(&s, sbuf, sizeof(sbuf));

    /* Act */
    blackbox_record_type ht;
    blackbox_record_type st;
    blackbox_record_result hr = blackbox_record_classify(hbuf, sizeof(hbuf), &ht);
    blackbox_record_result sr = blackbox_record_classify(sbuf, sizeof(sbuf), &st);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_OK, hr);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_TYPE_HEADER, ht);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_OK, sr);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_TYPE_SAMPLE, st);
}

static void test_decode_sample_rejects_header_slot(void)
{
    /* Arrange: a valid header slot fed to the sample decoder. */
    blackbox_session_header h = make_header();
    uint8_t buf[BLACKBOX_RECORD_SIZE];
    blackbox_record_encode_header(&h, buf, sizeof(buf));

    /* Act */
    blackbox_sample out;
    blackbox_record_result decoded =
        blackbox_record_decode_sample(buf, sizeof(buf), &out);

    /* Assert: type mismatch, not silently decoded as a sample. */
    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_ERR_TYPE, decoded);
}

/* ---- rejection paths (empty / corrupt / schema) ---- */

static void test_erased_sector_is_empty_not_data(void)
{
    /* Arrange: a freshly erased flash slot reads back as all 0xFF. */
    uint8_t buf[BLACKBOX_RECORD_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    /* Act */
    blackbox_record_type type;
    blackbox_record_result classified =
        blackbox_record_classify(buf, sizeof(buf), &type);
    blackbox_sample out;
    blackbox_record_result decoded =
        blackbox_record_decode_sample(buf, sizeof(buf), &out);

    /* Assert: recognised as EMPTY (a ring boundary), never mistaken for OK. */
    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_EMPTY, classified);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_EMPTY, decoded);
}

static void test_corrupt_crc_is_rejected(void)
{
    /* Arrange: flip a payload byte so the stored CRC no longer matches. */
    blackbox_sample in = make_sample();
    uint8_t buf[BLACKBOX_RECORD_SIZE];
    blackbox_record_encode_sample(&in, buf, sizeof(buf));
    buf[6] ^= 0xFFU; /* inside the payload, before the CRC trailer */

    /* Act */
    blackbox_sample out;
    blackbox_record_result decoded =
        blackbox_record_decode_sample(buf, sizeof(buf), &out);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_ERR_CRC, decoded);
}

static void test_wrong_magic_is_rejected(void)
{
    /* Arrange: a non-erased slot with a bad magic is corruption, not EMPTY. */
    blackbox_sample in = make_sample();
    uint8_t buf[BLACKBOX_RECORD_SIZE];
    blackbox_record_encode_sample(&in, buf, sizeof(buf));
    buf[0] ^= 0x01U; /* break the magic low byte */

    /* Act */
    blackbox_record_type type;
    blackbox_record_result classified =
        blackbox_record_classify(buf, sizeof(buf), &type);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_ERR_MAGIC, classified);
}

static void test_wrong_schema_is_rejected(void)
{
    /* Arrange: bump the schema byte and re-stamp the CRC so only the schema is
     * wrong (CRC would otherwise reject first). */
    blackbox_sample in = make_sample();
    uint8_t buf[BLACKBOX_RECORD_SIZE];
    blackbox_record_encode_sample(&in, buf, sizeof(buf));
    buf[2] = (uint8_t)(BLACKBOX_RECORD_SCHEMA + 1U); /* schema byte */
    uint32_t crc = blackbox_record_crc32(buf, BLACKBOX_RECORD_SIZE - 4U);
    buf[BLACKBOX_RECORD_SIZE - 4U] = (uint8_t)(crc & 0xFFU);
    buf[BLACKBOX_RECORD_SIZE - 3U] = (uint8_t)((crc >> 8) & 0xFFU);
    buf[BLACKBOX_RECORD_SIZE - 2U] = (uint8_t)((crc >> 16) & 0xFFU);
    buf[BLACKBOX_RECORD_SIZE - 1U] = (uint8_t)((crc >> 24) & 0xFFU);

    /* Act */
    blackbox_record_type type;
    blackbox_record_result classified =
        blackbox_record_classify(buf, sizeof(buf), &type);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_ERR_SCHEMA, classified);
}

static void test_wrong_length_is_rejected(void)
{
    blackbox_sample in = make_sample();
    uint8_t buf[BLACKBOX_RECORD_SIZE];
    blackbox_record_encode_sample(&in, buf, sizeof(buf));

    blackbox_sample out;
    blackbox_record_result decoded =
        blackbox_record_decode_sample(buf, BLACKBOX_RECORD_SIZE - 1U, &out);

    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_ERR_LENGTH, decoded);
}

static void test_encode_into_too_small_buffer_is_rejected(void)
{
    blackbox_sample in = make_sample();
    uint8_t buf[BLACKBOX_RECORD_SIZE];

    blackbox_record_result encoded =
        blackbox_record_encode_sample(&in, buf, BLACKBOX_RECORD_SIZE - 1U);

    TEST_ASSERT_EQUAL_INT(BLACKBOX_REC_ERR_LENGTH, encoded);
}

static void test_null_args_are_rejected(void)
{
    blackbox_sample s = make_sample();
    uint8_t buf[BLACKBOX_RECORD_SIZE];
    blackbox_record_encode_sample(&s, buf, sizeof(buf));
    blackbox_sample out;

    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_REC_ERR_ARG,
        blackbox_record_encode_sample(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_REC_ERR_ARG,
        blackbox_record_encode_sample(&s, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_REC_ERR_ARG,
        blackbox_record_decode_sample(buf, sizeof(buf), NULL));
}

void run_blackbox_record_tests(void)
{
    RUN_TEST(test_crc32_check_value_is_standard);
    RUN_TEST(test_sample_round_trip_preserves_every_field);
    RUN_TEST(test_sample_flags_false_round_trip);
    RUN_TEST(test_header_round_trip_preserves_every_field);
    RUN_TEST(test_classify_reports_record_type);
    RUN_TEST(test_decode_sample_rejects_header_slot);
    RUN_TEST(test_erased_sector_is_empty_not_data);
    RUN_TEST(test_corrupt_crc_is_rejected);
    RUN_TEST(test_wrong_magic_is_rejected);
    RUN_TEST(test_wrong_schema_is_rejected);
    RUN_TEST(test_wrong_length_is_rejected);
    RUN_TEST(test_encode_into_too_small_buffer_is_rejected);
    RUN_TEST(test_null_args_are_rejected);
}
