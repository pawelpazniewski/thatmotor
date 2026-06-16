#include <stdint.h>

#include "commit_debounce.h"
#include "unity.h"

/* Fixed window inside [MIN,MAX] so "now" arithmetic is easy to reason about. */
#define WINDOW_MS 3000U

static commit_debounce_state make_state(void)
{
    commit_debounce_state s;
    commit_debounce_init(&s, WINDOW_MS);
    return s;
}

static void test_init_is_clean_no_commit(void)
{
    /* Arrange */
    commit_debounce_state s = make_state();

    /* Act / Assert: nothing pending -> never commits, even with force. */
    TEST_ASSERT_FALSE(commit_debounce_should_commit(&s, 0, false));
    TEST_ASSERT_FALSE(commit_debounce_should_commit(&s, 999999U, true));
}

static void test_change_does_not_commit_before_window(void)
{
    /* Arrange: change at t=1000. */
    commit_debounce_state s = make_state();
    commit_debounce_mark_changed(&s, 1000U);

    /* Act / Assert: one ms before the window elapses -> no commit. */
    TEST_ASSERT_FALSE(commit_debounce_should_commit(&s, 1000U + WINDOW_MS - 1U,
                                                    false));
}

static void test_commit_exactly_at_window_boundary_is_inclusive(void)
{
    /* Arrange: change at t=1000. */
    commit_debounce_state s = make_state();
    commit_debounce_mark_changed(&s, 1000U);

    /* Act / Assert: exactly at last_change + window -> commit due (inclusive). */
    TEST_ASSERT_TRUE(commit_debounce_should_commit(&s, 1000U + WINDOW_MS,
                                                   false));
}

static void test_commit_after_window(void)
{
    /* Arrange */
    commit_debounce_state s = make_state();
    commit_debounce_mark_changed(&s, 1000U);

    /* Act / Assert: well past the window -> commit due. */
    TEST_ASSERT_TRUE(commit_debounce_should_commit(&s, 1000U + WINDOW_MS + 500U,
                                                   false));
}

static void test_second_change_resets_the_timer(void)
{
    /* Arrange: change at 1000, then again at 2500 (before the first elapsed). */
    commit_debounce_state s = make_state();
    commit_debounce_mark_changed(&s, 1000U);
    commit_debounce_mark_changed(&s, 2500U);

    /* Act / Assert: at 1000+window the first would have fired, but the reset
     * pushes the due time to 2500+window. Not due yet, due after. */
    TEST_ASSERT_FALSE(commit_debounce_should_commit(&s, 1000U + WINDOW_MS,
                                                    false));
    TEST_ASSERT_TRUE(commit_debounce_should_commit(&s, 2500U + WINDOW_MS,
                                                   false));
}

static void test_force_commits_immediately_when_dirty(void)
{
    /* Arrange: a change just happened, window not elapsed. */
    commit_debounce_state s = make_state();
    commit_debounce_mark_changed(&s, 1000U);

    /* Act / Assert: explicit Save commits now regardless of the timer. */
    TEST_ASSERT_TRUE(commit_debounce_should_commit(&s, 1000U, true));
}

static void test_force_does_nothing_when_clean(void)
{
    /* Arrange: clean state (no change). */
    commit_debounce_state s = make_state();

    /* Act / Assert: force only commits when there is something dirty. */
    TEST_ASSERT_FALSE(commit_debounce_should_commit(&s, 1000U, true));
}

static void test_mark_committed_clears_dirty(void)
{
    /* Arrange: change, then commit. */
    commit_debounce_state s = make_state();
    commit_debounce_mark_changed(&s, 1000U);
    commit_debounce_mark_committed(&s);

    /* Act / Assert: no longer due, even after the window or with force. */
    TEST_ASSERT_FALSE(commit_debounce_should_commit(&s, 1000U + WINDOW_MS,
                                                    false));
    TEST_ASSERT_FALSE(commit_debounce_should_commit(&s, 1000U + WINDOW_MS, true));
}

static void test_window_clamped_below_minimum(void)
{
    /* Arrange: request a sub-minimum window. */
    commit_debounce_state s;
    commit_debounce_init(&s, 100U);
    commit_debounce_mark_changed(&s, 0U);

    /* Act / Assert: clamped up to MIN; not due just before, due at MIN. */
    TEST_ASSERT_FALSE(commit_debounce_should_commit(
        &s, COMMIT_DEBOUNCE_MIN_MS - 1U, false));
    TEST_ASSERT_TRUE(commit_debounce_should_commit(&s, COMMIT_DEBOUNCE_MIN_MS,
                                                   false));
}

static void test_window_clamped_above_maximum(void)
{
    /* Arrange: request a super-maximum window. */
    commit_debounce_state s;
    commit_debounce_init(&s, 60000U);
    commit_debounce_mark_changed(&s, 0U);

    /* Act / Assert: clamped down to MAX; due exactly at MAX. */
    TEST_ASSERT_FALSE(commit_debounce_should_commit(
        &s, COMMIT_DEBOUNCE_MAX_MS - 1U, false));
    TEST_ASSERT_TRUE(commit_debounce_should_commit(&s, COMMIT_DEBOUNCE_MAX_MS,
                                                   false));
}

void run_commit_debounce_tests(void)
{
    RUN_TEST(test_init_is_clean_no_commit);
    RUN_TEST(test_change_does_not_commit_before_window);
    RUN_TEST(test_commit_exactly_at_window_boundary_is_inclusive);
    RUN_TEST(test_commit_after_window);
    RUN_TEST(test_second_change_resets_the_timer);
    RUN_TEST(test_force_commits_immediately_when_dirty);
    RUN_TEST(test_force_does_nothing_when_clean);
    RUN_TEST(test_mark_committed_clears_dirty);
    RUN_TEST(test_window_clamped_below_minimum);
    RUN_TEST(test_window_clamped_above_maximum);
}
