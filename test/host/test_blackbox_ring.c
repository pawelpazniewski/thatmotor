#include <stdint.h>

#include "blackbox_region.h"
#include "blackbox_ring.h"
#include "unity.h"

/* Small synthetic geometry keeps the wrap arithmetic easy to reason about:
 * 8 records total, 4 per sector -> 2 sectors. */
#define CAP 8U
#define RPS 4U
#define REC_SIZE 64U

/* ---- slot mapping across the capacity wrap ---- */

static void test_slot_wraps_at_capacity_boundary(void)
{
    /* Just before the wrap the slot is the last one; at capacity it returns to
     * 0; one past capacity is slot 1. */
    TEST_ASSERT_EQUAL_UINT32(CAP - 1U, blackbox_ring_slot(CAP - 1U, CAP));
    TEST_ASSERT_EQUAL_UINT32(0U, blackbox_ring_slot(CAP, CAP));
    TEST_ASSERT_EQUAL_UINT32(1U, blackbox_ring_slot(CAP + 1U, CAP));
}

static void test_overflow_seq_overwrites_oldest_slot(void)
{
    /* Oracle power: feed a seq POZA capacity. seq=CAP+3 must land on the SAME
     * physical slot as seq=3, i.e. the newest overwrites the oldest. A naive
     * `slot = seq` (or a saturating clamp) would fail this. */
    uint32_t fresh = blackbox_ring_slot(3U, CAP);
    uint32_t wrapped = blackbox_ring_slot(CAP + 3U, CAP);

    TEST_ASSERT_EQUAL_UINT32(3U, fresh);
    TEST_ASSERT_EQUAL_UINT32(fresh, wrapped);

    /* Two full laps on: seq=2*CAP+5 still collides with seq=5. */
    TEST_ASSERT_EQUAL_UINT32(blackbox_ring_slot(5U, CAP),
                             blackbox_ring_slot(2U * CAP + 5U, CAP));
}

static void test_offset_is_slot_times_record_size(void)
{
    TEST_ASSERT_EQUAL_UINT32(3U * REC_SIZE,
                             blackbox_ring_offset(3U, CAP, REC_SIZE));
    /* Wrapped seq keeps the offset inside the region (overwrites, not appends). */
    TEST_ASSERT_EQUAL_UINT32(3U * REC_SIZE,
                             blackbox_ring_offset(CAP + 3U, CAP, REC_SIZE));
}

/* ---- sector erase decision ---- */

static void test_needs_erase_only_on_first_record_of_sector(void)
{
    /* Slots 0 and 4 begin the two sectors -> erase; the rest do not. */
    TEST_ASSERT_TRUE(blackbox_ring_needs_erase(0U, CAP, RPS));
    TEST_ASSERT_FALSE(blackbox_ring_needs_erase(1U, CAP, RPS));
    TEST_ASSERT_FALSE(blackbox_ring_needs_erase(3U, CAP, RPS));
    TEST_ASSERT_TRUE(blackbox_ring_needs_erase(RPS, CAP, RPS));
    TEST_ASSERT_FALSE(blackbox_ring_needs_erase(RPS + 1U, CAP, RPS));
}

static void test_needs_erase_survives_capacity_wrap(void)
{
    /* After a full lap, seq=CAP maps to slot 0 -> a new sector, erase again. */
    TEST_ASSERT_TRUE(blackbox_ring_needs_erase(CAP, CAP, RPS));
    TEST_ASSERT_TRUE(blackbox_ring_needs_erase(CAP + RPS, CAP, RPS));
    TEST_ASSERT_FALSE(blackbox_ring_needs_erase(CAP + 2U, CAP, RPS));
}

/* ---- wrap-safe seq ordering (the u32 boundary is the whole point) ---- */

static void test_seq_after_within_range(void)
{
    TEST_ASSERT_TRUE(blackbox_ring_seq_after(10U, 9U));
    TEST_ASSERT_FALSE(blackbox_ring_seq_after(9U, 10U));
    TEST_ASSERT_FALSE(blackbox_ring_seq_after(5U, 5U));
}

static void test_seq_after_across_u32_wrap(void)
{
    /* Oracle power at the 2^32 boundary: seq 0 is the record written right
     * AFTER UINT32_MAX (0 == UINT32_MAX + 1). A naive `a > b` would call 0
     * older than UINT32_MAX and get the ordering backwards. */
    TEST_ASSERT_TRUE(blackbox_ring_seq_after(0U, UINT32_MAX));
    TEST_ASSERT_FALSE(blackbox_ring_seq_after(UINT32_MAX, 0U));

    /* A couple of steps past the wrap still rank newer than a couple before. */
    TEST_ASSERT_TRUE(blackbox_ring_seq_after(2U, UINT32_MAX - 2U));
    TEST_ASSERT_FALSE(blackbox_ring_seq_after(UINT32_MAX - 2U, 2U));
}

/* ---- oldest live sequence (read order start) ---- */

static void test_oldest_seq_before_wrap(void)
{
    /* 5 live records ending at seq 20 -> oldest is 16. */
    TEST_ASSERT_EQUAL_UINT32(16U, blackbox_ring_oldest_seq(20U, 5U));
    /* A single live record: oldest == newest. */
    TEST_ASSERT_EQUAL_UINT32(20U, blackbox_ring_oldest_seq(20U, 1U));
}

static void test_oldest_seq_wraps_through_zero(void)
{
    /* Oracle power: newest just past the wrap (seq 1), 4 live records
     * {1, 0, MAX, MAX-1}. The oldest is 1 - 3 = UINT32_MAX - 1, via wrap-safe
     * modular subtraction. A saturating subtraction clamped at 0 would give the
     * wrong start-of-read. */
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 1U,
                             blackbox_ring_oldest_seq(1U, 4U));
    /* And it must rank older than the newest under the wrap-safe comparator. */
    TEST_ASSERT_TRUE(blackbox_ring_seq_after(1U, blackbox_ring_oldest_seq(1U, 4U)));
}

/* ---- real geometry sanity (region constants line up) ---- */

static void test_real_geometry_constants_are_consistent(void)
{
    /* Capacity = region / record; records-per-sector = sector / record; both
     * divide evenly so no record straddles a sector. */
    TEST_ASSERT_EQUAL_UINT32(16384U, BLACKBOX_CAPACITY_RECORDS);
    TEST_ASSERT_EQUAL_UINT32(64U, BLACKBOX_RECORDS_PER_SECTOR);
    TEST_ASSERT_EQUAL_UINT32(0U, BLACKBOX_SECTOR_SIZE % BLACKBOX_RECORD_SIZE);
    TEST_ASSERT_EQUAL_UINT32(0U, BLACKBOX_REGION_SIZE % BLACKBOX_SECTOR_SIZE);
    /* Last slot of the real ring maps just below the region end. */
    TEST_ASSERT_EQUAL_UINT32(
        BLACKBOX_REGION_SIZE - BLACKBOX_RECORD_SIZE,
        blackbox_ring_offset(BLACKBOX_CAPACITY_RECORDS - 1U,
                             BLACKBOX_CAPACITY_RECORDS, BLACKBOX_RECORD_SIZE));
}

void run_blackbox_ring_tests(void)
{
    RUN_TEST(test_slot_wraps_at_capacity_boundary);
    RUN_TEST(test_overflow_seq_overwrites_oldest_slot);
    RUN_TEST(test_offset_is_slot_times_record_size);
    RUN_TEST(test_needs_erase_only_on_first_record_of_sector);
    RUN_TEST(test_needs_erase_survives_capacity_wrap);
    RUN_TEST(test_seq_after_within_range);
    RUN_TEST(test_seq_after_across_u32_wrap);
    RUN_TEST(test_oldest_seq_before_wrap);
    RUN_TEST(test_oldest_seq_wraps_through_zero);
    RUN_TEST(test_real_geometry_constants_are_consistent);
}
