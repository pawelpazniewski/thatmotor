#include <string.h>

#include "api_contract.h"
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

void run_goto_target_tests(void)
{
    RUN_TEST(test_target_inside_range_is_valid);
    RUN_TEST(test_lat_just_above_max_is_invalid);
    RUN_TEST(test_lon_just_below_min_is_invalid);
    RUN_TEST(test_lat_just_below_min_is_invalid);
    RUN_TEST(test_lon_just_above_max_is_invalid);
    RUN_TEST(test_exact_boundaries_are_valid);
    RUN_TEST(test_out_of_range_goto_builds_validation_error_envelope);
}
