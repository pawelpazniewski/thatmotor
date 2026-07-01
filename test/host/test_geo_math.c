#include "geo_math.h"

#include "unity.h"

/* Reference point: ~52 deg N, 21 deg E (mid-latitude, non-equatorial so the
 * cos(lat) longitude correction is clearly exercised). */
#define REF_LAT_E7 520000000
#define REF_LON_E7 210000000

/* 0.001 deg of latitude ~= R * 0.001 * pi/180 = 111.19 m. */
#define DEG_MILLI_E7 10000        /* 0.001 deg in degrees * 1e7 */
#define EXPECTED_111M 111.19f
#define DIST_TOL_M 1.0f

/* Bearing tolerance: +-1.0 deg expressed in deg10. */
#define BEARING_TOL_DEG10 10

void test_point_north_yields_bearing_zero(void)
{
    /* Arrange: a point due north of the reference (lat up, lon equal). */
    geo_offset off = geo_offset_m(REF_LAT_E7 + DEG_MILLI_E7, REF_LON_E7,
                                  REF_LAT_E7, REF_LON_E7);
    /* Act */
    uint16_t bearing = geo_bearing_deg10(off);

    /* Assert: bearing ~= 0 (due north). */
    TEST_ASSERT_INT_WITHIN(BEARING_TOL_DEG10, 0, (int)bearing);
    TEST_ASSERT_TRUE(off.north_m > 0.0f);
}

void test_point_east_yields_bearing_ninety(void)
{
    /* Arrange: a point due east of the reference (lon up, lat equal). */
    geo_offset off = geo_offset_m(REF_LAT_E7, REF_LON_E7 + DEG_MILLI_E7,
                                  REF_LAT_E7, REF_LON_E7);
    /* Act */
    uint16_t bearing = geo_bearing_deg10(off);

    /* Assert: bearing ~= 900 (90 deg, east). */
    TEST_ASSERT_INT_WITHIN(BEARING_TOL_DEG10, 900, (int)bearing);
}

void test_point_south_yields_bearing_one_eighty(void)
{
    /* Arrange: a point due south of the reference (lat down). */
    geo_offset off = geo_offset_m(REF_LAT_E7 - DEG_MILLI_E7, REF_LON_E7,
                                  REF_LAT_E7, REF_LON_E7);
    /* Act */
    uint16_t bearing = geo_bearing_deg10(off);

    /* Assert: bearing ~= 1800 (180 deg, south). */
    TEST_ASSERT_INT_WITHIN(BEARING_TOL_DEG10, 1800, (int)bearing);
}

void test_point_west_yields_bearing_two_seventy(void)
{
    /* Arrange: a point due west of the reference (lon down). */
    geo_offset off = geo_offset_m(REF_LAT_E7, REF_LON_E7 - DEG_MILLI_E7,
                                  REF_LAT_E7, REF_LON_E7);
    /* Act */
    uint16_t bearing = geo_bearing_deg10(off);

    /* Assert: bearing ~= 2700 (270 deg, west). */
    TEST_ASSERT_INT_WITHIN(BEARING_TOL_DEG10, 2700, (int)bearing);
}

void test_known_latitude_distance_matches_oracle(void)
{
    /* Arrange: 0.001 deg of latitude north of the reference. */
    geo_offset off = geo_offset_m(REF_LAT_E7 + DEG_MILLI_E7, REF_LON_E7,
                                  REF_LAT_E7, REF_LON_E7);
    /* Act */
    float dist = geo_distance_m(off);

    /* Assert: ~111 m within tolerance. */
    TEST_ASSERT_FLOAT_WITHIN(DIST_TOL_M, EXPECTED_111M, dist);
}

void test_zero_offset_yields_zero_distance_and_defined_bearing(void)
{
    /* Arrange: the point IS the reference -> zero offset. */
    geo_offset off = geo_offset_m(REF_LAT_E7, REF_LON_E7, REF_LAT_E7,
                                  REF_LON_E7);
    /* Act */
    float dist = geo_distance_m(off);
    uint16_t bearing = geo_bearing_deg10(off);

    /* Assert: distance exactly 0; bearing defined as 0. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, dist);
    TEST_ASSERT_EQUAL_INT(0, (int)bearing);
}

void test_cos_latitude_correction_shrinks_east_at_high_latitude(void)
{
    /* Arrange: the SAME longitude delta evaluated at the equator and at 60 deg
     * north. With the cos(lat) correction, east distance at 60 deg is ~half the
     * equatorial value (cos 60 = 0.5). Without the correction the two are
     * identical and this test FAILS (oracle power for the cos term). */
    int32_t lon_delta_e7 = 100000; /* 0.01 deg */

    geo_offset off_equator = geo_offset_m(0, lon_delta_e7, 0, 0);
    geo_offset off_high = geo_offset_m(600000000, 600000000 + lon_delta_e7,
                                       600000000, 600000000);

    /* Assert: high-latitude east distance is markedly smaller (~half). */
    TEST_ASSERT_TRUE(off_high.east_m > 0.0f);
    TEST_ASSERT_TRUE(off_high.east_m < off_equator.east_m * 0.6f);
    TEST_ASSERT_TRUE(off_high.east_m > off_equator.east_m * 0.4f);
}

void run_geo_math_tests(void)
{
    RUN_TEST(test_point_north_yields_bearing_zero);
    RUN_TEST(test_point_east_yields_bearing_ninety);
    RUN_TEST(test_point_south_yields_bearing_one_eighty);
    RUN_TEST(test_point_west_yields_bearing_two_seventy);
    RUN_TEST(test_known_latitude_distance_matches_oracle);
    RUN_TEST(test_zero_offset_yields_zero_distance_and_defined_bearing);
    RUN_TEST(test_cos_latitude_correction_shrinks_east_at_high_latitude);
}
