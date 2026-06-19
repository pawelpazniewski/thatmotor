#include "nmea_parse.h"

#include <string.h>

#include "unity.h"

/* Known-good fixtures with valid checksums (from the NMEA spec examples). */
#define GGA_FIX "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"
#define GGA_NO_FIX "$GPGGA,123519,4807.038,N,01131.000,E,0,00,99.9,,M,,M,,*45"

static gps_state zero_state(void)
{
    gps_state st;
    memset(&st, 0, sizeof(st));
    return st;
}

void test_gga_with_fix_decodes_position_and_sats(void)
{
    /* Arrange */
    gps_state st = zero_state();

    /* Act */
    bool updated = nmea_parse_line(GGA_FIX, &st);

    /* Assert: 4807.038 N = 48 + 7.038/60 = 48.1173 deg; 01131.000 E = 11.5167. */
    TEST_ASSERT_TRUE(updated);
    TEST_ASSERT_TRUE(st.fix);
    TEST_ASSERT_EQUAL_UINT8(8, st.sats);
    TEST_ASSERT_INT32_WITHIN(200, 481173000, st.lat_e7);
    TEST_ASSERT_INT32_WITHIN(200, 115166666, st.lon_e7);
}

void test_gga_without_fix_clears_fix(void)
{
    /* Arrange */
    gps_state st = zero_state();
    st.fix = true;

    /* Act */
    bool updated = nmea_parse_line(GGA_NO_FIX, &st);

    /* Assert */
    TEST_ASSERT_TRUE(updated);
    TEST_ASSERT_FALSE(st.fix);
}

void test_bad_checksum_returns_false_state_unchanged(void)
{
    /* Arrange: flip the checksum so it no longer matches the body. */
    gps_state st = zero_state();
    const char *bad =
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00";

    /* Act */
    bool updated = nmea_parse_line(bad, &st);

    /* Assert: rejected and the state is left at its initial (no-fix) value. */
    TEST_ASSERT_FALSE(updated);
    TEST_ASSERT_FALSE(st.fix);
    TEST_ASSERT_EQUAL_INT32(0, st.lat_e7);
    TEST_ASSERT_EQUAL_UINT8(0, st.sats);
}

void test_rmc_decodes_speed_and_course(void)
{
    /* Arrange: 22.4 knots = 41.5 km/h = 1152 cm/s; course 84.4 -> 84 deg. */
    gps_state st = zero_state();
    const char *rmc =
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";

    /* Act */
    bool updated = nmea_parse_line(rmc, &st);

    /* Assert: 22.4 kn * 51.4444 cm/s/kn = 1152.3 cm/s. */
    TEST_ASSERT_TRUE(updated);
    TEST_ASSERT_INT_WITHIN(3, 1152, st.speed_cms);
    TEST_ASSERT_EQUAL_UINT16(84, st.course_deg);
}

void test_vtg_decodes_speed_kmh(void)
{
    /* Arrange: 8.250 km/h = 229.2 cm/s; course 54.7 -> 54 deg. */
    gps_state st = zero_state();
    const char *vtg = "$GPVTG,054.7,T,034.4,M,005.5,N,008.250,K*44";

    /* Act */
    bool updated = nmea_parse_line(vtg, &st);

    /* Assert: 8.25 km/h * 27.778 cm/s/(km/h) = 229.2 cm/s. */
    TEST_ASSERT_TRUE(updated);
    TEST_ASSERT_INT_WITHIN(3, 229, st.speed_cms);
    TEST_ASSERT_EQUAL_UINT16(54, st.course_deg);
}

void test_southern_western_hemisphere_is_negative(void)
{
    /* Arrange: same magnitudes as GGA_FIX but S/W hemispheres. */
    gps_state st = zero_state();
    const char *sw =
        "$GPGGA,123519,4807.038,S,01131.000,W,1,08,0.9,545.4,M,46.9,M,,*48";

    /* Act */
    bool updated = nmea_parse_line(sw, &st);

    /* Assert: signs flip, magnitudes match the N/E case. */
    TEST_ASSERT_TRUE(updated);
    TEST_ASSERT_TRUE(st.lat_e7 < 0);
    TEST_ASSERT_TRUE(st.lon_e7 < 0);
    TEST_ASSERT_INT32_WITHIN(200, -481173000, st.lat_e7);
    TEST_ASSERT_INT32_WITHIN(200, -115166666, st.lon_e7);
}

void test_short_incomplete_sentence_returns_false(void)
{
    /* Arrange: truncated GGA with a valid checksum but too few fields. */
    gps_state st = zero_state();
    const char *partial = "$GPGGA,123519,4807.038,N*27";

    /* Act: must not crash and must not update. */
    bool updated = nmea_parse_line(partial, &st);

    /* Assert */
    TEST_ASSERT_FALSE(updated);
    TEST_ASSERT_FALSE(st.fix);
}

void run_nmea_parse_tests(void)
{
    RUN_TEST(test_gga_with_fix_decodes_position_and_sats);
    RUN_TEST(test_gga_without_fix_clears_fix);
    RUN_TEST(test_bad_checksum_returns_false_state_unchanged);
    RUN_TEST(test_rmc_decodes_speed_and_course);
    RUN_TEST(test_vtg_decodes_speed_kmh);
    RUN_TEST(test_southern_western_hemisphere_is_negative);
    RUN_TEST(test_short_incomplete_sentence_returns_false);
}
