#include "unity.h"
#include "ws_client_set.h"

/* --- add: empty -> ADDED, membership + count reflect the insert --- */

static void test_add_to_empty_is_added(void)
{
    ws_client_set set;
    ws_client_set_init(&set);

    TEST_ASSERT_EQUAL_INT(WS_CLIENT_ADDED, ws_client_set_add(&set, 7));
    TEST_ASSERT_EQUAL_UINT(1u, ws_client_set_count(&set));
    TEST_ASSERT_TRUE(ws_client_set_contains(&set, 7));
    /* An unrelated fd is not a member. */
    TEST_ASSERT_FALSE(ws_client_set_contains(&set, 8));
}

/* --- add: duplicate -> ALREADY_PRESENT, count unchanged (idempotent) --- */

static void test_add_duplicate_is_idempotent(void)
{
    ws_client_set set;
    ws_client_set_init(&set);
    TEST_ASSERT_EQUAL_INT(WS_CLIENT_ADDED, ws_client_set_add(&set, 7));

    /* Re-adding the same fd must report ALREADY_PRESENT and not grow the set.
     * Oracle power: an implementation that consumed a second slot for the dup
     * would report count==2 here. */
    TEST_ASSERT_EQUAL_INT(WS_CLIENT_ALREADY_PRESENT, ws_client_set_add(&set, 7));
    TEST_ASSERT_EQUAL_UINT(1u, ws_client_set_count(&set));
    TEST_ASSERT_TRUE(ws_client_set_contains(&set, 7));
}

/* --- add: beyond capacity -> FULL, set left intact (oracle power) --- */

static void test_add_beyond_capacity_is_full_and_intact(void)
{
    ws_client_set set;
    ws_client_set_init(&set);
    /* Fill every slot with distinct fds. */
    for (int fd = 0; fd < WS_TELEMETRY_MAX_CLIENTS; ++fd) {
        TEST_ASSERT_EQUAL_INT(WS_CLIENT_ADDED, ws_client_set_add(&set, fd));
    }
    TEST_ASSERT_EQUAL_UINT((unsigned)WS_TELEMETRY_MAX_CLIENTS,
                           ws_client_set_count(&set));

    /* The overflow add must be rejected and the set untouched. Oracle power: a
     * mutation that overwrote a slot "despite full" would either drop one of the
     * original fds (contains fails) or admit the intruder (contains(99) true). */
    int intruder = 99;
    TEST_ASSERT_EQUAL_INT(WS_CLIENT_FULL, ws_client_set_add(&set, intruder));
    TEST_ASSERT_EQUAL_UINT((unsigned)WS_TELEMETRY_MAX_CLIENTS,
                           ws_client_set_count(&set));
    TEST_ASSERT_FALSE(ws_client_set_contains(&set, intruder));
    for (int fd = 0; fd < WS_TELEMETRY_MAX_CLIENTS; ++fd) {
        TEST_ASSERT_TRUE(ws_client_set_contains(&set, fd));
    }
}

/* --- remove: middle member -> count-1, others kept, iteration has no holes --- */

static void test_remove_middle_keeps_others_no_holes(void)
{
    ws_client_set set;
    ws_client_set_init(&set);
    /* Requires capacity for three; guards the fixture if MAX shrinks. */
    TEST_ASSERT_TRUE(WS_TELEMETRY_MAX_CLIENTS >= 3);
    ws_client_set_add(&set, 10);
    ws_client_set_add(&set, 20);
    ws_client_set_add(&set, 30);

    TEST_ASSERT_TRUE(ws_client_set_remove(&set, 20));
    TEST_ASSERT_EQUAL_UINT(2u, ws_client_set_count(&set));
    TEST_ASSERT_TRUE(ws_client_set_contains(&set, 10));
    TEST_ASSERT_FALSE(ws_client_set_contains(&set, 20));
    TEST_ASSERT_TRUE(ws_client_set_contains(&set, 30));

    /* Iteration over [0, count) yields the two survivors with no free-slot hole
     * (-1) leaking through. */
    size_t n = ws_client_set_count(&set);
    bool seen10 = false, seen30 = false;
    for (size_t i = 0; i < n; ++i) {
        int fd = ws_client_set_at(&set, i);
        TEST_ASSERT_NOT_EQUAL(-1, fd);
        if (fd == 10) seen10 = true;
        if (fd == 30) seen30 = true;
    }
    TEST_ASSERT_TRUE(seen10);
    TEST_ASSERT_TRUE(seen30);
}

/* --- remove: absent fd -> false, set unchanged --- */

static void test_remove_absent_is_false_and_unchanged(void)
{
    ws_client_set set;
    ws_client_set_init(&set);
    ws_client_set_add(&set, 10);
    ws_client_set_add(&set, 20);

    TEST_ASSERT_FALSE(ws_client_set_remove(&set, 999));
    TEST_ASSERT_EQUAL_UINT(2u, ws_client_set_count(&set));
    TEST_ASSERT_TRUE(ws_client_set_contains(&set, 10));
    TEST_ASSERT_TRUE(ws_client_set_contains(&set, 20));
}

/* --- init: fresh set is empty and a slot can be reused after removal --- */

static void test_slot_is_reusable_after_removal(void)
{
    ws_client_set set;
    ws_client_set_init(&set);
    TEST_ASSERT_EQUAL_UINT(0u, ws_client_set_count(&set));

    /* Fill up, free one, and confirm the freed slot accepts a new fd. */
    for (int fd = 0; fd < WS_TELEMETRY_MAX_CLIENTS; ++fd) {
        ws_client_set_add(&set, fd);
    }
    TEST_ASSERT_TRUE(ws_client_set_remove(&set, 0));
    TEST_ASSERT_EQUAL_INT(WS_CLIENT_ADDED, ws_client_set_add(&set, 55));
    TEST_ASSERT_EQUAL_UINT((unsigned)WS_TELEMETRY_MAX_CLIENTS,
                           ws_client_set_count(&set));
    TEST_ASSERT_TRUE(ws_client_set_contains(&set, 55));
}

/* --- at: out-of-range index returns the free sentinel --- */

static void test_at_out_of_range_returns_sentinel(void)
{
    ws_client_set set;
    ws_client_set_init(&set);
    ws_client_set_add(&set, 10);

    TEST_ASSERT_EQUAL_INT(10, ws_client_set_at(&set, 0));
    TEST_ASSERT_EQUAL_INT(-1, ws_client_set_at(&set, 1));
    TEST_ASSERT_EQUAL_INT(-1, ws_client_set_at(&set, 99));
}

void run_ws_client_set_tests(void)
{
    RUN_TEST(test_add_to_empty_is_added);
    RUN_TEST(test_add_duplicate_is_idempotent);
    RUN_TEST(test_add_beyond_capacity_is_full_and_intact);
    RUN_TEST(test_remove_middle_keeps_others_no_holes);
    RUN_TEST(test_remove_absent_is_false_and_unchanged);
    RUN_TEST(test_slot_is_reusable_after_removal);
    RUN_TEST(test_at_out_of_range_returns_sentinel);
}
