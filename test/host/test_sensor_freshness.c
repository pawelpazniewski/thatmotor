#include "sensor_freshness.h"
#include "unity.h"

#include <stdint.h>

/* A threshold typical of the GPS staleness window (~1.5 s). */
#define THRESHOLD_MS 1500U

/* --- sensor_is_fresh: threshold boundary --- */

static void test_below_threshold_is_fresh(void)
{
    /* Arrange: 1499 ms elapsed, threshold 1500. Act + Assert. */
    TEST_ASSERT_TRUE(sensor_is_fresh(1499U, 0U, THRESHOLD_MS));
}

static void test_above_threshold_is_stale(void)
{
    /* Arrange: 1501 ms elapsed > threshold. */
    TEST_ASSERT_FALSE(sensor_is_fresh(1501U, 0U, THRESHOLD_MS));
}

static void test_exactly_at_threshold_is_stale(void)
{
    /* Boundary contract: elapsed == threshold counts as STALE (strict <). */
    TEST_ASSERT_FALSE(sensor_is_fresh(1500U, 0U, THRESHOLD_MS));
}

static void test_zero_elapsed_is_fresh(void)
{
    /* A reading stamped this very instant is fresh. */
    TEST_ASSERT_TRUE(sensor_is_fresh(42U, 42U, THRESHOLD_MS));
}

/* --- sensor_is_fresh: uint32 wrap boundary (the oracle: this FAILS for a
 * naive signed/unsafe (now - last) that ignores modular arithmetic). --- */

static void test_wrap_recent_reading_is_fresh(void)
{
    /* last stamped 100 ms before the 2^32 wrap; now is 200 ms after wrap.
     * True elapsed = 300 ms < threshold -> fresh. Modular subtraction
     * (now - last) = 200 - (2^32 - 100) = 300 (mod 2^32). A naive comparison
     * that treated this as a huge positive interval would wrongly report
     * stale, so this test has oracle power over the wrap-safe implementation. */
    uint32_t last_ms = (uint32_t)(UINT32_MAX - 99U); /* ~100 ms before wrap */
    uint32_t now_ms = 200U;                          /* 200 ms after wrap */
    TEST_ASSERT_TRUE(sensor_is_fresh(now_ms, last_ms, THRESHOLD_MS));
}

static void test_wrap_old_reading_is_stale(void)
{
    /* Same wrap setup but now is 2000 ms past the stamp (> threshold). */
    uint32_t last_ms = (uint32_t)(UINT32_MAX - 99U);
    uint32_t now_ms = 1900U; /* true elapsed = 100 + 1900 = 2000 ms */
    TEST_ASSERT_FALSE(sensor_is_fresh(now_ms, last_ms, THRESHOLD_MS));
}

/* --- sensor_freshness_stamp: refresh only on a valid reading --- */

static void test_stamp_advances_on_valid_reading(void)
{
    /* Arrange: previous stamp 1000, now 5000, reading valid. */
    TEST_ASSERT_EQUAL_UINT32(5000U,
                             sensor_freshness_stamp(1000U, 5000U, true));
}

static void test_stamp_held_on_invalid_reading(void)
{
    /* A fixless/invalid reading must NOT reset the staleness timer. */
    TEST_ASSERT_EQUAL_UINT32(1000U,
                             sensor_freshness_stamp(1000U, 5000U, false));
}

void run_sensor_freshness_tests(void)
{
    RUN_TEST(test_below_threshold_is_fresh);
    RUN_TEST(test_above_threshold_is_stale);
    RUN_TEST(test_exactly_at_threshold_is_stale);
    RUN_TEST(test_zero_elapsed_is_fresh);
    RUN_TEST(test_wrap_recent_reading_is_fresh);
    RUN_TEST(test_wrap_old_reading_is_stale);
    RUN_TEST(test_stamp_advances_on_valid_reading);
    RUN_TEST(test_stamp_held_on_invalid_reading);
}
