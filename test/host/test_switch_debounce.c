#include "switch_debounce.h"
#include "unity.h"

/* Shared fixture constants: a threshold and band typical of the firmware. */
#define TEST_THRESHOLD_US 1500U
#define TEST_SANITY_MIN_US 800U
#define TEST_SANITY_MAX_US 2200U
#define TEST_DEBOUNCE_FRAMES 3U

#define HIGH_US 1900U     /* >= threshold, in band */
#define LOW_US 1100U      /* < threshold, in band */
#define FLOATING_US 7322U /* out of band (floating pin) */

static switch_debounce_cfg default_cfg(void)
{
    switch_debounce_cfg cfg = {
        .threshold_us = TEST_THRESHOLD_US,
        .sanity_min_us = TEST_SANITY_MIN_US,
        .sanity_max_us = TEST_SANITY_MAX_US,
        .debounce_frames = TEST_DEBOUNCE_FRAMES,
    };
    return cfg;
}

static rc_channel_sample sample_at(uint32_t width_us)
{
    rc_channel_sample s = {
        .width_us = width_us,
        .period_us = 20000U,
        .last_edge_ticks = 0U,
        .edge_seen = true,
    };
    return s;
}

/* Feed the same width for n frames; return the LAST event. Used to settle the
 * debounce and to assert that holding a position emits nothing further. */
static switch_debounce_event feed(switch_debounce_state *st,
                                  const switch_debounce_cfg *cfg,
                                  uint32_t width_us, int frames)
{
    rc_channel_sample s = sample_at(width_us);
    switch_debounce_event last = SWITCH_DEBOUNCE_NONE;
    for (int i = 0; i < frames; ++i) {
        last = switch_debounce_update(st, &s, cfg);
    }
    return last;
}

/* Drive the state past the initial baseline (low, established) so later edges
 * are real directional events. Returns once initialised at "low". */
static void prime_low(switch_debounce_state *st, const switch_debounce_cfg *cfg)
{
    switch_debounce_event event = feed(st, cfg, LOW_US, TEST_DEBOUNCE_FRAMES);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, event); /* baseline only, no event */
}

static void test_rising_edge_emits_to_high_after_debounce(void)
{
    /* Arrange: initialised at low. */
    switch_debounce_state st;
    switch_debounce_init(&st);
    switch_debounce_cfg cfg = default_cfg();
    prime_low(&st, &cfg);
    rc_channel_sample high = sample_at(HIGH_US);

    /* Act: high for fewer than debounce frames -> no event yet. */
    switch_debounce_event early1 = switch_debounce_update(&st, &high, &cfg);
    switch_debounce_event early2 = switch_debounce_update(&st, &high, &cfg);
    switch_debounce_event edge = switch_debounce_update(&st, &high, &cfg);

    /* Assert: exactly one TO_HIGH, on the debounce-completing frame. */
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, early1);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, early2);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_TO_HIGH, edge);
}

static void test_falling_edge_emits_to_low_after_debounce(void)
{
    /* Arrange: initialised at low, then driven high. */
    switch_debounce_state st;
    switch_debounce_init(&st);
    switch_debounce_cfg cfg = default_cfg();
    prime_low(&st, &cfg);
    switch_debounce_event up = feed(&st, &cfg, HIGH_US, TEST_DEBOUNCE_FRAMES);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_TO_HIGH, up);

    /* Act: flick back down for the full debounce window. */
    switch_debounce_event down = feed(&st, &cfg, LOW_US, TEST_DEBOUNCE_FRAMES);

    /* Assert: high->low emits exactly TO_LOW. */
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_TO_LOW, down);
}

static void test_holding_high_emits_no_repeat(void)
{
    /* Arrange: initialised, then a real TO_HIGH edge. */
    switch_debounce_state st;
    switch_debounce_init(&st);
    switch_debounce_cfg cfg = default_cfg();
    prime_low(&st, &cfg);
    switch_debounce_event edge = feed(&st, &cfg, HIGH_US, TEST_DEBOUNCE_FRAMES);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_TO_HIGH, edge);

    /* Act: keep holding high for many more frames. */
    switch_debounce_event held = feed(&st, &cfg, HIGH_US, 10);

    /* Assert: no further event while held high. */
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, held);
}

static void test_holding_low_emits_no_repeat(void)
{
    /* Arrange: initialised at low (baseline). */
    switch_debounce_state st;
    switch_debounce_init(&st);
    switch_debounce_cfg cfg = default_cfg();
    prime_low(&st, &cfg);

    /* Act: keep holding low for many more frames. */
    switch_debounce_event held = feed(&st, &cfg, LOW_US, 10);

    /* Assert: holding low never emits an event. */
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, held);
}

static void test_short_noise_below_debounce_emits_none(void)
{
    /* Arrange: initialised at low. */
    switch_debounce_state st;
    switch_debounce_init(&st);
    switch_debounce_cfg cfg = default_cfg();
    prime_low(&st, &cfg);
    rc_channel_sample high = sample_at(HIGH_US);
    rc_channel_sample low = sample_at(LOW_US);

    /* Act: a single high frame (debounce is 3), then back to low. */
    switch_debounce_event spike = switch_debounce_update(&st, &high, &cfg);
    switch_debounce_event after1 = switch_debounce_update(&st, &low, &cfg);
    switch_debounce_event after2 = switch_debounce_update(&st, &low, &cfg);

    /* Assert: a sub-debounce glitch never produces an event. */
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, spike);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, after1);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, after2);
}

static void test_out_of_band_holds_level_no_false_edge(void)
{
    /* Arrange: initialised at low, then driven high (accepted). */
    switch_debounce_state st;
    switch_debounce_init(&st);
    switch_debounce_cfg cfg = default_cfg();
    prime_low(&st, &cfg);
    switch_debounce_event up = feed(&st, &cfg, HIGH_US, TEST_DEBOUNCE_FRAMES);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_TO_HIGH, up);

    /* Act: a floating pin reads 7322 us (out of [800,2200]); it must NOT
     * fabricate a TO_LOW edge from the previously-high level. */
    switch_debounce_event floating = feed(&st, &cfg, FLOATING_US,
                                          TEST_DEBOUNCE_FRAMES + 2);
    /* Returning into band at high must also not re-emit (level unchanged). */
    switch_debounce_event resume = feed(&st, &cfg, HIGH_US, TEST_DEBOUNCE_FRAMES);

    /* Assert: out-of-band frames hold the level and emit nothing. */
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, floating);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, resume);
}

static void test_init_baseline_high_emits_none_then_real_edge(void)
{
    /* Arrange: switch already high at power-up. */
    switch_debounce_state st;
    switch_debounce_init(&st);
    switch_debounce_cfg cfg = default_cfg();

    /* Act: first stable level is "high" -> baseline only, no event (no auto-arm
     * on boot). A real flick down then up IS a directional event. */
    switch_debounce_event baseline = feed(&st, &cfg, HIGH_US,
                                          TEST_DEBOUNCE_FRAMES);
    switch_debounce_event down = feed(&st, &cfg, LOW_US, TEST_DEBOUNCE_FRAMES);
    switch_debounce_event up = feed(&st, &cfg, HIGH_US, TEST_DEBOUNCE_FRAMES);

    /* Assert: booting with the switch high does not arm; later edges do. */
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, baseline);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_TO_LOW, down);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_TO_HIGH, up);
}

static void test_threshold_is_configurable(void)
{
    /* Arrange: 1600 us is "low" at a 1700 threshold but "high" at 1500. */
    const uint32_t mid_us = 1600U;

    switch_debounce_state high_thr;
    switch_debounce_init(&high_thr);
    switch_debounce_cfg high_cfg = default_cfg();
    high_cfg.threshold_us = 1700U;
    prime_low(&high_thr, &high_cfg);

    switch_debounce_state low_thr;
    switch_debounce_init(&low_thr);
    switch_debounce_cfg low_cfg = default_cfg(); /* threshold 1500 */
    prime_low(&low_thr, &low_cfg);

    /* Act */
    switch_debounce_event high_event =
        feed(&high_thr, &high_cfg, mid_us, TEST_DEBOUNCE_FRAMES + 1);
    switch_debounce_event low_event =
        feed(&low_thr, &low_cfg, mid_us, TEST_DEBOUNCE_FRAMES);

    /* Assert: same input, threshold decides the high/low split. */
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, high_event);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_TO_HIGH, low_event);
}

/* --- CH3 spot-lock switch scenario: the same generic module drives CH3. The
 * TO_HIGH edge is "enter spot-lock", TO_LOW is "abort", and holding either
 * position must not repeat the event. --- */
static void test_ch3_enter_abort_and_hold(void)
{
    /* Arrange: CH3 baseline low (spot-lock off). */
    switch_debounce_state ch3;
    switch_debounce_init(&ch3);
    switch_debounce_cfg cfg = default_cfg();
    prime_low(&ch3, &cfg);

    /* Act + Assert: flick high -> enter (TO_HIGH); hold high -> no repeat;
     * flick low -> abort (TO_LOW); hold low -> no repeat. */
    switch_debounce_event enter = feed(&ch3, &cfg, HIGH_US, TEST_DEBOUNCE_FRAMES);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_TO_HIGH, enter);

    switch_debounce_event held_on = feed(&ch3, &cfg, HIGH_US, 5);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, held_on);

    switch_debounce_event abort = feed(&ch3, &cfg, LOW_US, TEST_DEBOUNCE_FRAMES);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_TO_LOW, abort);

    switch_debounce_event held_off = feed(&ch3, &cfg, LOW_US, 5);
    TEST_ASSERT_EQUAL(SWITCH_DEBOUNCE_NONE, held_off);
}

void run_switch_debounce_tests(void)
{
    RUN_TEST(test_rising_edge_emits_to_high_after_debounce);
    RUN_TEST(test_falling_edge_emits_to_low_after_debounce);
    RUN_TEST(test_holding_high_emits_no_repeat);
    RUN_TEST(test_holding_low_emits_no_repeat);
    RUN_TEST(test_short_noise_below_debounce_emits_none);
    RUN_TEST(test_out_of_band_holds_level_no_false_edge);
    RUN_TEST(test_init_baseline_high_emits_none_then_real_edge);
    RUN_TEST(test_threshold_is_configurable);
    RUN_TEST(test_ch3_enter_abort_and_hold);
}
