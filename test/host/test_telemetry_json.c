#include <stdint.h>
#include <string.h>

#include "telemetry_json.h"
#include "unity.h"

/* Fill every field with its type-max (widest) value so the serialised line is at
 * least as long as any real telemetry frame: all uint32 = UINT32_MAX (10 digits),
 * all *_e7 = INT32_MIN (11 chars incl. sign), all uint16 = 65535, all uint8 = 255,
 * signed trim = INT16_MIN, every bool = false ("false" is longer than "true").
 * Real RC widths/periods are far smaller, so passing here is conservative. */
static control_loop_snapshot max_width_snapshot(void)
{
    control_loop_snapshot s;
    memset(&s, 0, sizeof(s));
    /* Fill fw_version to its full 31-char capacity (the field is fixed-size,
     * NUL-terminated) -- an empty/zeroed string would understate the true
     * worst case this test exists to guard. */
    memset(s.fw_version, 'X', sizeof(s.fw_version) - 1);
    s.fw_version[sizeof(s.fw_version) - 1] = '\0';
    s.state = (sm_state)9;
    s.arm_reason = (sm_arm_reason)99;
    s.rc_valid = false;
    s.ch1_us = UINT32_MAX;
    s.ch2_us = UINT32_MAX;
    s.ch4_us = UINT32_MAX;
    s.ch3_us = UINT32_MAX;
    s.ch1_period_us = UINT32_MAX;
    s.ch2_period_us = UINT32_MAX;
    s.ch1_valid = false;
    s.ch2_valid = false;
    s.servo_us = UINT32_MAX;
    s.esc_us = UINT32_MAX;
    s.servo_trim_us = INT16_MIN;
    s.source = (settings_source)9;
    s.settings_valid = false;
    s.calibrated = false;
    s.defaults_used = false;
    s.nvs_error = false;
    s.gps_fix = false;
    s.gps_sats = UINT8_MAX;
    s.gps_lat_e7 = INT32_MIN;
    s.gps_lon_e7 = INT32_MIN;
    s.gps_speed_cms = UINT16_MAX;
    s.imu_ok = false;
    s.imu_heading_deg10 = UINT16_MAX;
    s.imu_calib = UINT8_MAX;
    s.spot_lock_state = UINT8_MAX;
    s.spot_lock_err_m = UINT16_MAX;
    s.spot_lock_bearing_deg10 = UINT16_MAX;
    s.goto_state = UINT8_MAX;
    s.goto_target_lat_e7 = INT32_MIN;
    s.goto_target_lon_e7 = INT32_MIN;
    s.goto_err_m = UINT16_MAX;
    s.goto_bearing_deg10 = UINT16_MAX;
    s.goto_arrived = false;
    s.app_link_fresh = false;
    return s;
}

/* --- GUARD (oracle power): the widest possible frame must fit the buffer that
 * ws_telemetry stack-allocates. Adding a field to telemetry_json_format without
 * bumping WS_TELEMETRY_JSON_MAX pushes len past the bound and fails HERE, in CI,
 * instead of silently on hardware (snprintf returns len >= size -> frame dropped
 * -> panel shows "--"). This is exactly the 640 B regression that shipped. --- */
static void test_max_width_frame_fits_buffer(void)
{
    control_loop_snapshot s = max_width_snapshot();
    char buf[4096]; /* oversized so we measure the true length, not truncation */
    int len = telemetry_json_format(&s, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN_INT(0, len);
    /* The whole point: worst-case length stays strictly under the ws buffer. */
    TEST_ASSERT_LESS_THAN_INT(WS_TELEMETRY_JSON_MAX, len);
    /* And the measured string length matches snprintf's report (no truncation). */
    TEST_ASSERT_EQUAL_UINT((size_t)len, strlen(buf));
}

/* --- Happy path: a typical frame serialises to well-formed, bounded JSON. --- */
static void test_typical_frame_is_bounded_json(void)
{
    control_loop_snapshot s;
    memset(&s, 0, sizeof(s));
    s.gps_fix = true;
    s.gps_sats = 12;
    s.gps_lat_e7 = 524321567;
    s.gps_lon_e7 = 210212345;
    s.goto_state = 1;

    char buf[WS_TELEMETRY_JSON_MAX];
    int len = telemetry_json_format(&s, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN_INT(0, len);
    TEST_ASSERT_LESS_THAN_INT(WS_TELEMETRY_JSON_MAX, len);
    TEST_ASSERT_EQUAL_INT('{', buf[0]);
    TEST_ASSERT_EQUAL_INT('}', buf[len - 1]);
}

/* --- Contract: on a too-small buffer, snprintf reports the would-be length
 * (>= size), which is how push_work detects truncation and drops the frame. --- */
static void test_truncation_reports_would_be_length(void)
{
    control_loop_snapshot s;
    memset(&s, 0, sizeof(s));

    char tiny[16];
    int len = telemetry_json_format(&s, tiny, sizeof(tiny));

    TEST_ASSERT_GREATER_OR_EQUAL_INT((int)sizeof(tiny), len);
}

void run_telemetry_json_tests(void)
{
    RUN_TEST(test_max_width_frame_fits_buffer);
    RUN_TEST(test_typical_frame_is_bounded_json);
    RUN_TEST(test_truncation_reports_would_be_length);
}
