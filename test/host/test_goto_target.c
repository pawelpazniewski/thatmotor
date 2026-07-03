#include <math.h>
#include <string.h>

#include "api_contract.h"
#include "goto_grab.h"
#include "goto_target.h"
#include "unity.h"

/* --- goto_target_valid: range oracle (out-of-range inputs, not identity) --- */

static void test_target_inside_range_is_valid(void)
{
    /* A plausible mid-latitude / mid-longitude target well inside the range. */
    TEST_ASSERT_TRUE(goto_target_valid(521000000, 210000000));
}

static void test_lat_just_above_max_is_invalid(void)
{
    /* +90 deg + 1e-7: strictly out of range on latitude. Oracle power: the
     * expected output (false) differs from the input being echoed. */
    TEST_ASSERT_FALSE(goto_target_valid(900000001, 0));
}

static void test_lon_just_below_min_is_invalid(void)
{
    /* -180 deg - 1e-7: strictly out of range on longitude. */
    TEST_ASSERT_FALSE(goto_target_valid(0, -1800000001));
}

static void test_lat_just_below_min_is_invalid(void)
{
    TEST_ASSERT_FALSE(goto_target_valid(-900000001, 0));
}

static void test_lon_just_above_max_is_invalid(void)
{
    TEST_ASSERT_FALSE(goto_target_valid(0, 1800000001));
}

/* Exact boundaries are inclusive: +/-90 lat and +/-180 lon must be accepted. */
static void test_exact_boundaries_are_valid(void)
{
    TEST_ASSERT_TRUE(goto_target_valid(GOTO_LAT_E7_MAX, GOTO_LON_E7_MAX));
    TEST_ASSERT_TRUE(goto_target_valid(GOTO_LAT_E7_MIN, GOTO_LON_E7_MIN));
    TEST_ASSERT_EQUAL_INT32(900000000, GOTO_LAT_E7_MAX);
    TEST_ASSERT_EQUAL_INT32(-900000000, GOTO_LAT_E7_MIN);
    TEST_ASSERT_EQUAL_INT32(1800000000, GOTO_LON_E7_MAX);
    TEST_ASSERT_EQUAL_INT32(-1800000000, GOTO_LON_E7_MIN);
}

/* --- Unit 2 contract: out-of-range goto -> 400 {data:null,error:{code}} --- */

/* Mirrors the HTTP handler decision using the same pure building blocks it uses:
 * an out-of-range target fails goto_target_valid, so the handler builds a
 * VALIDATION_FAILED error envelope (400) and never posts to the loop mailbox. */
static void test_out_of_range_goto_builds_validation_error_envelope(void)
{
    /* Arrange: an out-of-range target (oracle power: input != expected output). */
    int32_t bad_lat_e7 = 900000001;
    int32_t good_lon_e7 = 0;

    /* Act: the handler gate rejects, so it emits the error envelope. */
    TEST_ASSERT_FALSE(goto_target_valid(bad_lat_e7, good_lon_e7));
    char body[256];
    size_t len = api_build_error(API_ERR_VALIDATION_FAILED, NULL, body, sizeof(body));

    /* Assert: 400-shaped envelope with data:null and an error code. */
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(body, "\"data\":null"));
    TEST_ASSERT_NOT_NULL(strstr(body, "\"error\":{\"code\":\"VALIDATION_FAILED\""));
}

/* --- goto_target_from_double: double-domain validation before the int32 cast ---
 *
 * These close the oracle on inputs the int32 range oracle cannot see: non-finite
 * values (cast = UB) and magnitudes outside int32 that would wrap modulo 2^32
 * back into range. Each MUST be rejected; a naive `(int32_t)valuedouble` before
 * validation would let 4.39e9 through (wraps to ~1e8) -> these tests FAIL. */

static void test_from_double_valid_narrows_to_int32(void)
{
    int32_t lat_e7 = 0, lon_e7 = 0;
    TEST_ASSERT_TRUE(goto_target_from_double(521000000.0, 210000000.0, &lat_e7, &lon_e7));
    TEST_ASSERT_EQUAL_INT32(521000000, lat_e7);
    TEST_ASSERT_EQUAL_INT32(210000000, lon_e7);
}

static void test_from_double_infinity_is_rejected(void)
{
    int32_t lat_e7 = 7, lon_e7 = 7;
    TEST_ASSERT_FALSE(goto_target_from_double(INFINITY, 0.0, &lat_e7, &lon_e7));
    TEST_ASSERT_FALSE(goto_target_from_double(0.0, -INFINITY, &lat_e7, &lon_e7));
    /* Outputs untouched on rejection. */
    TEST_ASSERT_EQUAL_INT32(7, lat_e7);
    TEST_ASSERT_EQUAL_INT32(7, lon_e7);
}

static void test_from_double_nan_is_rejected(void)
{
    int32_t lat_e7 = 0, lon_e7 = 0;
    TEST_ASSERT_FALSE(goto_target_from_double(NAN, 0.0, &lat_e7, &lon_e7));
    TEST_ASSERT_FALSE(goto_target_from_double(0.0, NAN, &lat_e7, &lon_e7));
}

static void test_from_double_out_of_int32_wrap_is_rejected(void)
{
    /* 4.39e9 > INT32_MAX: a bare cast wraps to ~1e8 and would pass the range
     * oracle. Validated in the double domain, it is rejected. */
    int32_t lat_e7 = 0, lon_e7 = 0;
    TEST_ASSERT_FALSE(goto_target_from_double(4.39e9, 0.0, &lat_e7, &lon_e7));
    TEST_ASSERT_FALSE(goto_target_from_double(-4.39e9, 0.0, &lat_e7, &lon_e7));
    TEST_ASSERT_FALSE(goto_target_from_double(0.0, 4.39e9, &lat_e7, &lon_e7));
}

static void test_from_double_just_out_of_range_is_rejected(void)
{
    int32_t lat_e7 = 0, lon_e7 = 0;
    TEST_ASSERT_FALSE(goto_target_from_double(900000001.0, 0.0, &lat_e7, &lon_e7));
    TEST_ASSERT_FALSE(goto_target_from_double(0.0, -1800000001.0, &lat_e7, &lon_e7));
}

/* --- Unit 3: hold fix-grab decision (goto_grab_decide) --- */

static void test_grab_engages_on_fresh_in_range_fix(void)
{
    /* A fresh, real, in-range fix -> engage with the sampled coordinates as the
     * anchor target (target == the sampled input, passed through). */
    goto_grab_decision d = goto_grab_decide(true, true, 521000000, 210000000);

    TEST_ASSERT_TRUE(d.engage);
    TEST_ASSERT_EQUAL_INT32(521000000, d.lat_e7);
    TEST_ASSERT_EQUAL_INT32(210000000, d.lon_e7);
}

static void test_grab_blocked_without_real_fix(void)
{
    /* Seed-fresh: fresh window but NO real fix. Must NOT engage. Oracle power:
     * dropping the fix gate (engage on fresh alone) would return true here. */
    goto_grab_decision d = goto_grab_decide(true, false, 521000000, 210000000);

    TEST_ASSERT_FALSE(d.engage);
}

static void test_grab_blocked_when_stale(void)
{
    /* Not fresh -> must not engage (symmetric guard to the fix gate). */
    goto_grab_decision d = goto_grab_decide(false, true, 521000000, 210000000);

    TEST_ASSERT_FALSE(d.engage);
}

static void test_grab_blocked_out_of_range_fix(void)
{
    /* A garbage fix out of the geographic range must not engage even when fresh
     * with a real fix (mirrors goto_target_valid; oracle: input past +/-90 lat). */
    goto_grab_decision d = goto_grab_decide(true, true, 900000001, 0);
    TEST_ASSERT_FALSE(d.engage);

    goto_grab_decision d2 = goto_grab_decide(true, true, 0, -1800000001);
    TEST_ASSERT_FALSE(d2.engage);
}

void run_goto_target_tests(void)
{
    RUN_TEST(test_target_inside_range_is_valid);
    RUN_TEST(test_lat_just_above_max_is_invalid);
    RUN_TEST(test_lon_just_below_min_is_invalid);
    RUN_TEST(test_lat_just_below_min_is_invalid);
    RUN_TEST(test_lon_just_above_max_is_invalid);
    RUN_TEST(test_exact_boundaries_are_valid);
    RUN_TEST(test_out_of_range_goto_builds_validation_error_envelope);
    RUN_TEST(test_from_double_valid_narrows_to_int32);
    RUN_TEST(test_from_double_infinity_is_rejected);
    RUN_TEST(test_from_double_nan_is_rejected);
    RUN_TEST(test_from_double_out_of_int32_wrap_is_rejected);
    RUN_TEST(test_from_double_just_out_of_range_is_rejected);
    RUN_TEST(test_grab_engages_on_fresh_in_range_fix);
    RUN_TEST(test_grab_blocked_without_real_fix);
    RUN_TEST(test_grab_blocked_when_stale);
    RUN_TEST(test_grab_blocked_out_of_range_fix);
}
