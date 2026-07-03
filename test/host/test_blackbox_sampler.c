#include <stdint.h>

#include "blackbox_region.h"
#include "blackbox_sampler.h"
#include "unity.h"

/* Build a tick input with sane defaults (running session, nothing changed): a
 * test overrides only the field it exercises so the oracle is unambiguous. */
static blackbox_sampler_in tick(uint8_t prev, uint8_t cur)
{
    blackbox_sampler_in in = {
        .prev_substate = prev,
        .cur_substate = cur,
        .ms_since_sample = 0U,
        .err_m = 100U,
        .last_err_m = 100U,
        .event = false,
        .off_tail_left = 0U,
    };
    return in;
}

/* ---- session start / boundaries ---- */

static void test_off_to_active_starts_session(void)
{
    /* OFF -> ACTIVE is the leading edge of a session: emit a header, not a
     * sample. A naive "sample whenever non-OFF" would return SAMPLE here. */
    blackbox_sampler_in in = tick(BLACKBOX_SPOT_LOCK_OFF, BLACKBOX_SPOT_LOCK_ACTIVE);
    blackbox_sampler_out out = blackbox_sampler_step(&in);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_START_SESSION, out.action);
}

static void test_start_clears_any_stale_tail(void)
{
    /* A new session opening while a previous tail was still owed must reset the
     * tail counter, or the fresh session would emit phantom tail samples. */
    blackbox_sampler_in in = tick(BLACKBOX_SPOT_LOCK_OFF, BLACKBOX_SPOT_LOCK_ACTIVE);
    in.off_tail_left = 7U;
    blackbox_sampler_out out = blackbox_sampler_step(&in);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_START_SESSION, out.action);
    TEST_ASSERT_EQUAL_UINT16(0U, out.off_tail_left);
}

/* ---- adaptive rate inside a running session ---- */

static void test_idle_when_nothing_changed(void)
{
    /* Steady hold, error unchanged, no event, heartbeat not due -> SKIP. This is
     * the whole point of adaptive rate; the old "always SAMPLE" would fail here. */
    blackbox_sampler_in in = tick(BLACKBOX_SPOT_LOCK_ACTIVE, BLACKBOX_SPOT_LOCK_ACTIVE);
    in.ms_since_sample = BLACKBOX_HEARTBEAT_MS - 1U;
    blackbox_sampler_out out = blackbox_sampler_step(&in);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_IDLE, out.action);
}

static void test_event_forces_a_sample(void)
{
    blackbox_sampler_in in = tick(BLACKBOX_SPOT_LOCK_ACTIVE, BLACKBOX_SPOT_LOCK_ACTIVE);
    in.event = true;
    blackbox_sampler_out out = blackbox_sampler_step(&in);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_SAMPLE, out.action);
}

static void test_error_move_forces_a_sample_at_threshold(void)
{
    /* Oracle at the delta boundary: a move of exactly ERR_DELTA_M samples, a
     * move of one less (with nothing else) does not. */
    blackbox_sampler_in due = tick(BLACKBOX_SPOT_LOCK_ACTIVE, BLACKBOX_SPOT_LOCK_ACTIVE);
    due.err_m = due.last_err_m + BLACKBOX_ERR_DELTA_M;
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_SAMPLE, blackbox_sampler_step(&due).action);

    blackbox_sampler_in below = tick(BLACKBOX_SPOT_LOCK_ACTIVE, BLACKBOX_SPOT_LOCK_ACTIVE);
    below.err_m = below.last_err_m + (BLACKBOX_ERR_DELTA_M - 1U);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_IDLE, blackbox_sampler_step(&below).action);
}

static void test_heartbeat_forces_a_sample_at_threshold(void)
{
    /* Nothing changed, but the idle heartbeat elapsed -> one sample. */
    blackbox_sampler_in in = tick(BLACKBOX_SPOT_LOCK_ACTIVE, BLACKBOX_SPOT_LOCK_ACTIVE);
    in.ms_since_sample = BLACKBOX_HEARTBEAT_MS;
    blackbox_sampler_out out = blackbox_sampler_step(&in);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_SAMPLE, out.action);
}

static void test_pause_and_resume_always_sampled(void)
{
    /* R2: ACTIVE<->PAUSED stays inside the session AND is always interesting,
     * even with no event, unchanged error and heartbeat not due. */
    blackbox_sampler_in pause = tick(BLACKBOX_SPOT_LOCK_ACTIVE, BLACKBOX_SPOT_LOCK_PAUSED);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_SAMPLE, blackbox_sampler_step(&pause).action);

    blackbox_sampler_in resume = tick(BLACKBOX_SPOT_LOCK_PAUSED, BLACKBOX_SPOT_LOCK_ACTIVE);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_SAMPLE, blackbox_sampler_step(&resume).action);
}

/* ---- OFF tail (post-override drift capture) ---- */

static void test_drop_to_off_begins_tail(void)
{
    /* non-OFF -> OFF records the drop as a tail sample and arms the tail budget
     * (minus the one this tick spends). The old model closed with no samples. */
    blackbox_sampler_in in = tick(BLACKBOX_SPOT_LOCK_ACTIVE, BLACKBOX_SPOT_LOCK_OFF);
    blackbox_sampler_out out = blackbox_sampler_step(&in);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_SAMPLE_TAIL, out.action);
    TEST_ASSERT_EQUAL_UINT16(BLACKBOX_OFF_TAIL_SAMPLES - 1U, out.off_tail_left);

    /* PAUSED -> OFF behaves the same. */
    blackbox_sampler_in p = tick(BLACKBOX_SPOT_LOCK_PAUSED, BLACKBOX_SPOT_LOCK_OFF);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_SAMPLE_TAIL, blackbox_sampler_step(&p).action);
}

static void test_off_tail_drains_then_goes_idle(void)
{
    /* While OFF with budget owed, keep writing tail samples and count down. */
    blackbox_sampler_in in = tick(BLACKBOX_SPOT_LOCK_OFF, BLACKBOX_SPOT_LOCK_OFF);
    in.off_tail_left = 1U;
    blackbox_sampler_out out = blackbox_sampler_step(&in);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_SAMPLE_TAIL, out.action);
    TEST_ASSERT_EQUAL_UINT16(0U, out.off_tail_left);

    /* Budget exhausted: fall silent (the manual-driving guard). */
    blackbox_sampler_in done = tick(BLACKBOX_SPOT_LOCK_OFF, BLACKBOX_SPOT_LOCK_OFF);
    done.off_tail_left = 0U;
    blackbox_sampler_out idle = blackbox_sampler_step(&done);
    TEST_ASSERT_EQUAL_INT(BLACKBOX_ACTION_IDLE, idle.action);
    TEST_ASSERT_EQUAL_UINT16(0U, idle.off_tail_left);
}

/* ---- geometry consistency (capacity derives from region/record, Unit 1) ---- */

static void test_capacity_consistent_with_geometry(void)
{
    /* The recorder's ring capacity must equal region / record so a session's
     * samples address real slots. Ties the sampler's world to Unit 1 geometry. */
    TEST_ASSERT_EQUAL_UINT32(BLACKBOX_REGION_SIZE / BLACKBOX_RECORD_SIZE,
                             BLACKBOX_CAPACITY_RECORDS);
    TEST_ASSERT_EQUAL_UINT32(65536U, BLACKBOX_CAPACITY_RECORDS); /* 4 MiB / 64 B */
}

void run_blackbox_sampler_tests(void)
{
    RUN_TEST(test_off_to_active_starts_session);
    RUN_TEST(test_start_clears_any_stale_tail);
    RUN_TEST(test_idle_when_nothing_changed);
    RUN_TEST(test_event_forces_a_sample);
    RUN_TEST(test_error_move_forces_a_sample_at_threshold);
    RUN_TEST(test_heartbeat_forces_a_sample_at_threshold);
    RUN_TEST(test_pause_and_resume_always_sampled);
    RUN_TEST(test_drop_to_off_begins_tail);
    RUN_TEST(test_off_tail_drains_then_goes_idle);
    RUN_TEST(test_capacity_consistent_with_geometry);
}
