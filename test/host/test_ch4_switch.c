#include "ch4_switch.h"
#include "unity.h"

/* Shared fixture constants: a threshold and band typical of the firmware. */
#define TEST_THRESHOLD_US 1700U
#define TEST_SANITY_MIN_US 800U
#define TEST_SANITY_MAX_US 2200U
#define TEST_DEBOUNCE_FRAMES 3U

#define PRESSED_US 1900U   /* >= threshold, in band */
#define RELEASED_US 1100U  /* < threshold, in band */
#define FLOATING_US 7322U  /* out of band (floating pin) */

static ch4_switch_cfg default_cfg(void)
{
    ch4_switch_cfg cfg = {
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

/* Feed the same width for n frames; return the LAST pulse result. Used to settle
 * the debounce and to assert that holding emits nothing further. */
static bool feed(ch4_switch_state *st, const ch4_switch_cfg *cfg,
                 uint32_t width_us, int frames)
{
    rc_channel_sample s = sample_at(width_us);
    bool last = false;
    for (int i = 0; i < frames; ++i) {
        last = ch4_switch_update(st, &s, cfg);
    }
    return last;
}

/* Drive the state past the initial baseline (released, established) so later
 * edges are real toggles. Returns once initialised at "released". */
static void prime_released(ch4_switch_state *st, const ch4_switch_cfg *cfg)
{
    bool pulse = feed(st, cfg, RELEASED_US, TEST_DEBOUNCE_FRAMES);
    TEST_ASSERT_FALSE(pulse); /* baseline only, no pulse */
}

static void test_rising_edge_emits_one_pulse_after_debounce(void)
{
    /* Arrange: initialised at released. */
    ch4_switch_state st;
    ch4_switch_init(&st);
    ch4_switch_cfg cfg = default_cfg();
    prime_released(&st, &cfg);
    rc_channel_sample pressed = sample_at(PRESSED_US);

    /* Act: press for fewer than debounce frames -> no pulse yet. */
    bool early1 = ch4_switch_update(&st, &pressed, &cfg);
    bool early2 = ch4_switch_update(&st, &pressed, &cfg);
    bool edge = ch4_switch_update(&st, &pressed, &cfg); /* debounce reached */

    /* Assert: exactly one pulse, on the debounce-completing frame. */
    TEST_ASSERT_FALSE(early1);
    TEST_ASSERT_FALSE(early2);
    TEST_ASSERT_TRUE(edge);
}

static void test_holding_pressed_emits_no_repeat(void)
{
    /* Arrange: initialised, then a real press edge. */
    ch4_switch_state st;
    ch4_switch_init(&st);
    ch4_switch_cfg cfg = default_cfg();
    prime_released(&st, &cfg);
    bool edge = feed(&st, &cfg, PRESSED_US, TEST_DEBOUNCE_FRAMES);
    TEST_ASSERT_TRUE(edge);

    /* Act: keep holding the button for many more frames. */
    bool held = feed(&st, &cfg, PRESSED_US, 10);

    /* Assert: no further toggle while held. */
    TEST_ASSERT_FALSE(held);
}

static void test_falling_edge_emits_no_pulse(void)
{
    /* Arrange: initialised, pressed, then release. */
    ch4_switch_state st;
    ch4_switch_init(&st);
    ch4_switch_cfg cfg = default_cfg();
    prime_released(&st, &cfg);
    feed(&st, &cfg, PRESSED_US, TEST_DEBOUNCE_FRAMES);

    /* Act: release for the full debounce window. */
    bool release_pulse = feed(&st, &cfg, RELEASED_US, TEST_DEBOUNCE_FRAMES);

    /* Assert: releasing never toggles. */
    TEST_ASSERT_FALSE(release_pulse);
}

static void test_short_noise_below_debounce_emits_no_pulse(void)
{
    /* Arrange: initialised at released. */
    ch4_switch_state st;
    ch4_switch_init(&st);
    ch4_switch_cfg cfg = default_cfg();
    prime_released(&st, &cfg);
    rc_channel_sample pressed = sample_at(PRESSED_US);
    rc_channel_sample released = sample_at(RELEASED_US);

    /* Act: a single pressed frame (debounce is 3), then back to released. */
    bool spike = ch4_switch_update(&st, &pressed, &cfg);
    bool after1 = ch4_switch_update(&st, &released, &cfg);
    bool after2 = ch4_switch_update(&st, &released, &cfg);

    /* Assert: a sub-debounce glitch never produces a toggle. */
    TEST_ASSERT_FALSE(spike);
    TEST_ASSERT_FALSE(after1);
    TEST_ASSERT_FALSE(after2);
}

static void test_out_of_band_is_treated_as_released_no_pulse(void)
{
    /* Arrange: initialised at released. */
    ch4_switch_state st;
    ch4_switch_init(&st);
    ch4_switch_cfg cfg = default_cfg();
    prime_released(&st, &cfg);

    /* Act: a floating pin reads 7322 us (out of [800,2200]); above threshold
     * numerically but out of band, so it must NOT count as pressed. */
    bool pulse = feed(&st, &cfg, FLOATING_US, TEST_DEBOUNCE_FRAMES + 2);

    /* Assert: no toggle from an out-of-band signal. */
    TEST_ASSERT_FALSE(pulse);
}

static void test_initialisation_baseline_pressed_emits_no_pulse(void)
{
    /* Arrange: button already held at power-up. */
    ch4_switch_state st;
    ch4_switch_init(&st);
    ch4_switch_cfg cfg = default_cfg();

    /* Act: first stable level is "pressed" -> baseline only, no toggle. A real
     * release then re-press IS a toggle. */
    bool baseline = feed(&st, &cfg, PRESSED_US, TEST_DEBOUNCE_FRAMES);
    bool released = feed(&st, &cfg, RELEASED_US, TEST_DEBOUNCE_FRAMES);
    bool repress = feed(&st, &cfg, PRESSED_US, TEST_DEBOUNCE_FRAMES);

    /* Assert: booting with the button held does not arm; later edge does. */
    TEST_ASSERT_FALSE(baseline);
    TEST_ASSERT_FALSE(released);
    TEST_ASSERT_TRUE(repress);
}

static void test_threshold_is_configurable(void)
{
    /* Arrange: a width of 1600 us is "released" at the default 1700 threshold
     * but "pressed" once the threshold is lowered to 1500. */
    const uint32_t mid_us = 1600U;

    ch4_switch_state high;
    ch4_switch_init(&high);
    ch4_switch_cfg high_cfg = default_cfg(); /* threshold 1700 */
    prime_released(&high, &high_cfg);

    ch4_switch_state low;
    ch4_switch_init(&low);
    ch4_switch_cfg low_cfg = default_cfg();
    low_cfg.threshold_us = 1500U;
    prime_released(&low, &low_cfg);

    /* Act */
    bool high_pulse = feed(&high, &high_cfg, mid_us, TEST_DEBOUNCE_FRAMES + 1);
    bool low_pulse = feed(&low, &low_cfg, mid_us, TEST_DEBOUNCE_FRAMES);

    /* Assert: same input, threshold decides the press. */
    TEST_ASSERT_FALSE(high_pulse);
    TEST_ASSERT_TRUE(low_pulse);
}

void run_ch4_switch_tests(void)
{
    RUN_TEST(test_rising_edge_emits_one_pulse_after_debounce);
    RUN_TEST(test_holding_pressed_emits_no_repeat);
    RUN_TEST(test_falling_edge_emits_no_pulse);
    RUN_TEST(test_short_noise_below_debounce_emits_no_pulse);
    RUN_TEST(test_out_of_band_is_treated_as_released_no_pulse);
    RUN_TEST(test_initialisation_baseline_pressed_emits_no_pulse);
    RUN_TEST(test_threshold_is_configurable);
}
