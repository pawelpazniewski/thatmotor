#include <stdint.h>

#include "blackbox_region.h"
#include "blackbox_sampler.h"
#include "unity.h"

/* ---- session start / sampling / close edges ---- */

static void test_off_to_active_starts_session(void)
{
    /* OFF -> ACTIVE is the leading edge of a session: emit a header, not a
     * sample. A naive "sample whenever non-OFF" would return SAMPLE here. */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_ACTION_START_SESSION,
        blackbox_sampler_decide(BLACKBOX_SPOT_LOCK_OFF,
                                BLACKBOX_SPOT_LOCK_ACTIVE));
}

static void test_active_to_active_samples(void)
{
    /* Steady ACTIVE run: append a sample, never a second header. */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_ACTION_SAMPLE,
        blackbox_sampler_decide(BLACKBOX_SPOT_LOCK_ACTIVE,
                                BLACKBOX_SPOT_LOCK_ACTIVE));
}

static void test_active_to_paused_still_samples(void)
{
    /* R2: PAUSED is part of the same session. ACTIVE -> PAUSED must keep
     * sampling (not close), and it must not open a new session either. */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_ACTION_SAMPLE,
        blackbox_sampler_decide(BLACKBOX_SPOT_LOCK_ACTIVE,
                                BLACKBOX_SPOT_LOCK_PAUSED));
    /* PAUSED -> PAUSED and PAUSED -> ACTIVE stay inside the session too. */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_ACTION_SAMPLE,
        blackbox_sampler_decide(BLACKBOX_SPOT_LOCK_PAUSED,
                                BLACKBOX_SPOT_LOCK_PAUSED));
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_ACTION_SAMPLE,
        blackbox_sampler_decide(BLACKBOX_SPOT_LOCK_PAUSED,
                                BLACKBOX_SPOT_LOCK_ACTIVE));
}

static void test_nonoff_to_off_closes_session(void)
{
    /* Both non-OFF exits close the session; no more samples follow. */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_ACTION_CLOSE_SESSION,
        blackbox_sampler_decide(BLACKBOX_SPOT_LOCK_ACTIVE,
                                BLACKBOX_SPOT_LOCK_OFF));
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_ACTION_CLOSE_SESSION,
        blackbox_sampler_decide(BLACKBOX_SPOT_LOCK_PAUSED,
                                BLACKBOX_SPOT_LOCK_OFF));
}

static void test_off_to_off_is_idle(void)
{
    /* No session in flight: nothing to record. */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_ACTION_IDLE,
        blackbox_sampler_decide(BLACKBOX_SPOT_LOCK_OFF,
                                BLACKBOX_SPOT_LOCK_OFF));
}

/* ---- geometry consistency (capacity derives from region/record, Unit 1) ---- */

static void test_capacity_consistent_with_geometry(void)
{
    /* The recorder's ring capacity must equal region / record so a session's
     * samples address real slots. Ties the sampler's world to Unit 1 geometry. */
    TEST_ASSERT_EQUAL_UINT32(BLACKBOX_REGION_SIZE / BLACKBOX_RECORD_SIZE,
                             BLACKBOX_CAPACITY_RECORDS);
    TEST_ASSERT_EQUAL_UINT32(16384U, BLACKBOX_CAPACITY_RECORDS);
}

void run_blackbox_sampler_tests(void)
{
    RUN_TEST(test_off_to_active_starts_session);
    RUN_TEST(test_active_to_active_samples);
    RUN_TEST(test_active_to_paused_still_samples);
    RUN_TEST(test_nonoff_to_off_closes_session);
    RUN_TEST(test_off_to_off_is_idle);
    RUN_TEST(test_capacity_consistent_with_geometry);
}
