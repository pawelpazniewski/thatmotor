#include <string.h>

#include "blackbox_csv.h"
#include "blackbox_record.h"
#include "unity.h"

/* A sample with every field a distinct value so a wrong column order surfaces as
 * a mismatched value, not a coincidental pass. */
static blackbox_sample make_sample(void)
{
    blackbox_sample s = {
        .t_ms = 12345U,
        .substate = 1U,
        .sm_state = 1U,
        .source = 2U,
        .end_reason = 0U,
        .arm_reason = 0U,
        .err_m = 7U,
        .bearing_deg10 = 1801U,
        .heading_deg10 = 900U,
        .servo_us = 1500U,
        .esc_us = 1600U,
        .ch1_us = 1490U,
        .ch2_us = 1510U,
        .ch3_us = 1900U,
        .ch4_us = 1100U,
        .lat_e7 = -123456789,
        .lon_e7 = 987654321,
        .sats = 9U,
        .speed_cms = 42U,
        .imu_calib = 3U,
        .gps_fix = true,
        .imu_ok = false,
        .rc_valid = true,
        .gps_fresh = true,
        .link_fresh = false,
        .goto_owns = true,
        .arrived = false,
    };
    return s;
}

static blackbox_session_header make_header(uint32_t id)
{
    blackbox_session_header h = {
        .session_seq = id,
        .target_lat_e7 = 111111111,
        .target_lon_e7 = -222222222,
        .deadband_m = 3U,
        .max_throttle_pct = 35U,
        .throttle_gain = 30U,
        .servo_gain = 20U,
        .start_ms = 5000U,
    };
    return h;
}

/* ---- row column order + values ---- */

static void test_row_exact_column_order_and_values(void)
{
    blackbox_session_header h = make_header(4U);
    blackbox_sample s = make_sample();

    char row[BLACKBOX_CSV_LINE_MAX];
    size_t n = blackbox_csv_row(&h, &s, row, sizeof(row));

    /* Full-string oracle: session_id, then the sample columns in order, then the
     * session target + settings tail. gps_fix=1, imu_ok=0. */
    const char *expected =
        "4,12345,1,1,2,0,0,7,1801,900,1500,1600,1490,1510,1900,1100,"
        "-123456789,987654321,9,42,3,1,0,1,1,0,1,0,"
        "111111111,-222222222,3,35,30,20";
    TEST_ASSERT_EQUAL_STRING(expected, row);
    TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
}

static void test_row_rejects_too_small_buffer(void)
{
    blackbox_session_header h = make_header(1U);
    blackbox_sample s = make_sample();

    char tiny[8];
    /* Too small for the full row -> 0 (no truncated, unparseable line). */
    TEST_ASSERT_EQUAL_UINT(0U, blackbox_csv_row(&h, &s, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_UINT(0U, blackbox_csv_row(NULL, &s, tiny, sizeof(tiny)));
}

/* ---- header line matches the row column order (parser contract) ---- */

static void test_header_matches_field_order(void)
{
    char hdr[BLACKBOX_CSV_LINE_MAX];
    size_t n = blackbox_csv_header(hdr, sizeof(hdr));

    const char *expected =
        "session_id,t_ms,substate,sm_state,source,end_reason,arm_reason,err_m,"
        "bearing_deg10,heading_deg10,servo_us,esc_us,ch1_us,ch2_us,ch3_us,ch4_us,"
        "lat_e7,lon_e7,sats,speed_cms,imu_calib,gps_fix,imu_ok,rc_valid,gps_fresh,"
        "link_fresh,goto_owns,arrived,target_lat_e7,target_lon_e7,deadband_m,"
        "max_throttle_pct,throttle_gain,servo_gain";
    TEST_ASSERT_EQUAL_STRING(expected, hdr);
    TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
}

static void test_header_column_count_matches_row_commas(void)
{
    /* The header and a row must carry the same number of columns. Count commas
     * in each and confirm both equal BLACKBOX_CSV_COLUMN_COUNT - 1. */
    char hdr[BLACKBOX_CSV_LINE_MAX];
    blackbox_csv_header(hdr, sizeof(hdr));

    blackbox_session_header h = make_header(1U);
    blackbox_sample s = make_sample();
    char row[BLACKBOX_CSV_LINE_MAX];
    blackbox_csv_row(&h, &s, row, sizeof(row));

    unsigned hdr_commas = 0U;
    for (const char *p = hdr; *p; ++p) {
        if (*p == ',') {
            hdr_commas++;
        }
    }
    unsigned row_commas = 0U;
    for (const char *p = row; *p; ++p) {
        if (*p == ',') {
            row_commas++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(BLACKBOX_CSV_COLUMN_COUNT - 1U, hdr_commas);
    TEST_ASSERT_EQUAL_UINT(hdr_commas, row_commas);
}

/* ---- one session's N samples share session_id + settings ---- */

static void test_session_rows_share_id_and_settings(void)
{
    blackbox_session_header h = make_header(7U);

    /* Three samples with different telemetry but the same session context. Each
     * row must start with the same session id and end with the same settings
     * tail regardless of the per-sample values. */
    for (unsigned i = 0; i < 3U; ++i) {
        blackbox_sample s = make_sample();
        s.t_ms = 1000U * (i + 1U);
        s.err_m = (uint16_t)(10U - i);

        char row[BLACKBOX_CSV_LINE_MAX];
        size_t n = blackbox_csv_row(&h, &s, row, sizeof(row));
        TEST_ASSERT_TRUE(n > 0U);

        /* Leading session id column. */
        TEST_ASSERT_EQUAL_INT(0, strncmp(row, "7,", 2));
        /* Trailing denormalised settings tail (target + 4 spot-lock params). */
        TEST_ASSERT_NOT_NULL(strstr(row, ",111111111,-222222222,3,35,30,20"));
    }
}

void run_blackbox_csv_tests(void)
{
    RUN_TEST(test_row_exact_column_order_and_values);
    RUN_TEST(test_row_rejects_too_small_buffer);
    RUN_TEST(test_header_matches_field_order);
    RUN_TEST(test_header_column_count_matches_row_commas);
    RUN_TEST(test_session_rows_share_id_and_settings);
}
