#include "click_counter.h"
#include "unity.h"

#define WINDOW_FRAMES 25U /* e.g. 500 ms / 20 ms = 25 frames */

/* Drive `frames` silent frames and return the last gesture observed (the gesture
 * that closes a burst lands on a single frame). */
static click_gesture run_silence(click_counter_state *st, uint16_t frames)
{
    click_gesture g = CLICK_NONE;
    for (uint16_t i = 0; i < frames; i++) {
        g = click_counter_update(st, false, WINDOW_FRAMES);
    }
    return g;
}

static void test_single_click_then_window_yields_single(void)
{
    /* Arrange */
    click_counter_state st;
    click_counter_reset(&st);

    /* Act: one click, then let the full window elapse with no further click. */
    TEST_ASSERT_EQUAL_INT(CLICK_NONE,
                          click_counter_update(&st, true, WINDOW_FRAMES));
    click_gesture closed = run_silence(&st, WINDOW_FRAMES);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(CLICK_SINGLE, closed);
}

static void test_two_clicks_then_window_yields_none(void)
{
    /* Arrange */
    click_counter_state st;
    click_counter_reset(&st);

    /* Act: two clicks within the window, then let the window close the burst. */
    click_counter_update(&st, true, WINDOW_FRAMES);
    click_counter_update(&st, false, WINDOW_FRAMES);
    click_counter_update(&st, true, WINDOW_FRAMES);
    click_gesture closed = run_silence(&st, WINDOW_FRAMES);

    /* Assert: 2 clicks resolve to nothing. */
    TEST_ASSERT_EQUAL_INT(CLICK_NONE, closed);
}

static void test_three_clicks_triple_fires_on_third_click(void)
{
    /* Arrange */
    click_counter_state st;
    click_counter_reset(&st);

    /* Act: three clicks inside the window; the TRIPLE must fire on the 3rd. */
    TEST_ASSERT_EQUAL_INT(CLICK_NONE,
                          click_counter_update(&st, true, WINDOW_FRAMES));
    TEST_ASSERT_EQUAL_INT(CLICK_NONE,
                          click_counter_update(&st, false, WINDOW_FRAMES));
    TEST_ASSERT_EQUAL_INT(CLICK_NONE,
                          click_counter_update(&st, true, WINDOW_FRAMES));
    TEST_ASSERT_EQUAL_INT(CLICK_NONE,
                          click_counter_update(&st, false, WINDOW_FRAMES));
    click_gesture third = click_counter_update(&st, true, WINDOW_FRAMES);

    /* Assert: immediate TRIPLE on the third click (no window wait). */
    TEST_ASSERT_EQUAL_INT(CLICK_TRIPLE, third);
}

static void test_clicks_spread_with_gaps_below_window_count(void)
{
    /* Arrange */
    click_counter_state st;
    click_counter_reset(&st);
    uint16_t gap = WINDOW_FRAMES - 1U; /* strictly inside the window */

    /* Act: three clicks separated by gaps just under the window still triple. */
    click_counter_update(&st, true, WINDOW_FRAMES);
    TEST_ASSERT_EQUAL_INT(CLICK_NONE, run_silence(&st, gap));
    click_counter_update(&st, true, WINDOW_FRAMES);
    TEST_ASSERT_EQUAL_INT(CLICK_NONE, run_silence(&st, gap));
    click_gesture third = click_counter_update(&st, true, WINDOW_FRAMES);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(CLICK_TRIPLE, third);
}

static void test_gap_over_window_resets_and_each_resolves_single(void)
{
    /* Arrange */
    click_counter_state st;
    click_counter_reset(&st);

    /* Act: a click, a full window (closes as SINGLE), then a second isolated
     * click + window. Each isolated click resolves independently as SINGLE; the
     * gap over the window must NOT let them accumulate toward a TRIPLE. */
    click_counter_update(&st, true, WINDOW_FRAMES);
    click_gesture first = run_silence(&st, WINDOW_FRAMES);

    click_counter_update(&st, true, WINDOW_FRAMES);
    click_gesture second = run_silence(&st, WINDOW_FRAMES);

    /* Assert */
    TEST_ASSERT_EQUAL_INT(CLICK_SINGLE, first);
    TEST_ASSERT_EQUAL_INT(CLICK_SINGLE, second);
}

static void test_reset_clears_open_burst(void)
{
    /* Arrange: open a 2-click burst. */
    click_counter_state st;
    click_counter_reset(&st);
    click_counter_update(&st, true, WINDOW_FRAMES);
    click_counter_update(&st, true, WINDOW_FRAMES);

    /* Act: reset mid-burst, then a single click + window. */
    click_counter_reset(&st);
    TEST_ASSERT_EQUAL_UINT8(0U, st.count);
    click_counter_update(&st, true, WINDOW_FRAMES);
    click_gesture closed = run_silence(&st, WINDOW_FRAMES);

    /* Assert: the post-reset click resolves as a fresh SINGLE (no carry-over). */
    TEST_ASSERT_EQUAL_INT(CLICK_SINGLE, closed);
}

void run_click_counter_tests(void)
{
    RUN_TEST(test_single_click_then_window_yields_single);
    RUN_TEST(test_two_clicks_then_window_yields_none);
    RUN_TEST(test_three_clicks_triple_fires_on_third_click);
    RUN_TEST(test_clicks_spread_with_gaps_below_window_count);
    RUN_TEST(test_gap_over_window_resets_and_each_resolves_single);
    RUN_TEST(test_reset_clears_open_burst);
}
