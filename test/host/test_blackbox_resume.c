#include <stdbool.h>
#include <stdint.h>

#include "blackbox_resume.h"
#include "unity.h"

/* Helpers to fold synthetic slots the way the HAL scan would. A "header" slot
 * carries a session id; a "sample" slot is valid but not a header; an "empty"
 * slot is an erased/corrupt slot that must advance nothing. */
static void feed_header(blackbox_resume_scan *scan, uint32_t slot,
                        uint32_t session_seq)
{
    blackbox_resume_scan_slot(scan, slot, true, true, session_seq);
}

static void feed_sample(blackbox_resume_scan *scan, uint32_t slot)
{
    blackbox_resume_scan_slot(scan, slot, true, false, 0U);
}

static void feed_empty(blackbox_resume_scan *scan, uint32_t slot)
{
    blackbox_resume_scan_slot(scan, slot, false, false, 0U);
}

/* ---- empty region: fresh start ---- */

static void test_empty_region_starts_fresh(void)
{
    /* Nothing on flash: the recorder must start at cursor 0 and session id 0
     * (its first START then yields session id 1). */
    blackbox_resume_scan scan;
    blackbox_resume_scan_init(&scan);

    blackbox_resume_seed seed = blackbox_resume_decide(&scan);
    TEST_ASSERT_EQUAL_UINT32(0U, seed.cursor);
    TEST_ASSERT_EQUAL_UINT32(0U, seed.session_seq);
}

static void test_all_slots_empty_starts_fresh(void)
{
    /* A region full of erased slots is still a fresh start, not a resume. */
    blackbox_resume_scan scan;
    blackbox_resume_scan_init(&scan);
    for (uint32_t slot = 0U; slot < 64U; ++slot) {
        feed_empty(&scan, slot);
    }

    blackbox_resume_seed seed = blackbox_resume_decide(&scan);
    TEST_ASSERT_EQUAL_UINT32(0U, seed.cursor);
    TEST_ASSERT_EQUAL_UINT32(0U, seed.session_seq);
}

/* ---- resume past a pre-reboot outing (oracle: cursor must NOT be 0) ---- */

static void test_resume_after_reboot_continues_cursor_and_session(void)
{
    /* Outing before the reboot: header (session 1) at slot 0, then 99 samples
     * in slots 1..99, then the rest of the region erased. */
    blackbox_resume_scan scan;
    blackbox_resume_scan_init(&scan);
    feed_header(&scan, 0U, 1U);
    for (uint32_t slot = 1U; slot < 100U; ++slot) {
        feed_sample(&scan, slot);
    }
    feed_empty(&scan, 100U);
    feed_empty(&scan, 101U);

    blackbox_resume_seed seed = blackbox_resume_decide(&scan);

    /* Cursor resumes right AFTER the last valid record (slot 99) -> 100. This is
     * the oracle: init zeroing the cursor would put it at 0 and overwrite the
     * previous outing, so this assertion fails if the resume is dropped. */
    TEST_ASSERT_EQUAL_UINT32(100U, seed.cursor);
    /* Session seed is the highest existing id; the next START yields id + 1 = 2,
     * never reusing the previous outing's id 1. */
    TEST_ASSERT_EQUAL_UINT32(1U, seed.session_seq);
    TEST_ASSERT_EQUAL_UINT32(2U, seed.session_seq + 1U);
}

static void test_resume_picks_highest_session_across_outings(void)
{
    /* Three outings survive on flash with ids 1, 2, 3 (interleaved samples).
     * The session seed must be the highest (3), so the next id is 4. */
    blackbox_resume_scan scan;
    blackbox_resume_scan_init(&scan);
    feed_header(&scan, 0U, 1U);
    feed_sample(&scan, 1U);
    feed_header(&scan, 2U, 2U);
    feed_sample(&scan, 3U);
    feed_header(&scan, 4U, 3U);
    feed_sample(&scan, 5U);

    blackbox_resume_seed seed = blackbox_resume_decide(&scan);
    TEST_ASSERT_EQUAL_UINT32(3U, seed.session_seq);
    TEST_ASSERT_EQUAL_UINT32(6U, seed.cursor);
}

static void test_out_of_order_header_ids_take_the_max(void)
{
    /* Header order on flash need not be ascending by id (defensive against a
     * partial/legacy region). The seed is still the max id, not the last seen. */
    blackbox_resume_scan scan;
    blackbox_resume_scan_init(&scan);
    feed_header(&scan, 0U, 5U);
    feed_sample(&scan, 1U);
    feed_header(&scan, 2U, 3U);

    blackbox_resume_seed seed = blackbox_resume_decide(&scan);
    TEST_ASSERT_EQUAL_UINT32(5U, seed.session_seq);
}

/* ---- corruption / gaps: cursor tracks the highest valid slot ---- */

static void test_gap_does_not_shorten_cursor(void)
{
    /* A corrupt slot in the middle (slot 5) must not pull the cursor back: the
     * last valid record is slot 10, so the cursor resumes at 11 (using a count
     * of valid records would give 10 and collide with the record at slot 10). */
    blackbox_resume_scan scan;
    blackbox_resume_scan_init(&scan);
    feed_header(&scan, 0U, 1U);
    for (uint32_t slot = 1U; slot <= 10U; ++slot) {
        if (slot == 5U) {
            feed_empty(&scan, slot); /* corrupt/erased slot in the middle */
        } else {
            feed_sample(&scan, slot);
        }
    }

    blackbox_resume_seed seed = blackbox_resume_decide(&scan);
    TEST_ASSERT_EQUAL_UINT32(11U, seed.cursor);
}

static void test_trailing_empty_slots_ignored(void)
{
    /* Valid records only in slots 0..2, then erased tail: cursor is 3, and a
     * session with no header at all still resumes ids at 0. */
    blackbox_resume_scan scan;
    blackbox_resume_scan_init(&scan);
    feed_sample(&scan, 0U);
    feed_sample(&scan, 1U);
    feed_sample(&scan, 2U);
    for (uint32_t slot = 3U; slot < 50U; ++slot) {
        feed_empty(&scan, slot);
    }

    blackbox_resume_seed seed = blackbox_resume_decide(&scan);
    TEST_ASSERT_EQUAL_UINT32(3U, seed.cursor);
    TEST_ASSERT_EQUAL_UINT32(0U, seed.session_seq);
}

void run_blackbox_resume_tests(void)
{
    RUN_TEST(test_empty_region_starts_fresh);
    RUN_TEST(test_all_slots_empty_starts_fresh);
    RUN_TEST(test_resume_after_reboot_continues_cursor_and_session);
    RUN_TEST(test_resume_picks_highest_session_across_outings);
    RUN_TEST(test_out_of_order_header_ids_take_the_max);
    RUN_TEST(test_gap_does_not_shorten_cursor);
    RUN_TEST(test_trailing_empty_slots_ignored);
}
