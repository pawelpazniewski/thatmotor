#include "led_pattern.h"
#include "unity.h"

/* --- ARMED: solid on --- */

static void test_armed_is_solid_on_at_any_time(void)
{
    /* Act + Assert: ARMED is always on regardless of phase. */
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_ARMED, true, 0));
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_ARMED, true, 137));
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_ARMED, false, 5000));
}

/* --- DISARMED calibrated: slow 0.5 Hz, 50% duty --- */

static void test_disarmed_calibrated_on_in_first_half(void)
{
    /* Arrange: t=0 is the start of the on-half of the 2000 ms period. */
    /* Act + Assert */
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_DISARMED, true, 0));
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_DISARMED, true, 999));
}

static void test_disarmed_calibrated_off_in_second_half(void)
{
    /* Act + Assert: second half of the 2000 ms slow period is off. */
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_DISARMED, true, 1000));
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_DISARMED, true, 1999));
}

static void test_disarmed_calibrated_period_repeats(void)
{
    /* Act + Assert: pattern repeats every LED_PATTERN_DISARMED_PERIOD_MS. */
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_DISARMED, true,
                                    LED_PATTERN_DISARMED_PERIOD_MS));
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_DISARMED, true,
                                     LED_PATTERN_DISARMED_PERIOD_MS + 1000));
}

/* --- DISARMED uncalibrated: double-blink overlay (only when !calibrated) --- */

static void test_disarmed_uncalibrated_double_blink_first_slot_on(void)
{
    /* Arrange: slot 0 (0..149 ms) is the first short blink. */
    /* Act + Assert */
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_DISARMED, false, 0));
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_DISARMED, false, 100));
}

static void test_disarmed_uncalibrated_double_blink_gap_off(void)
{
    /* Arrange: slot 1 (150..299 ms) is the gap between the two blinks. */
    /* Act + Assert */
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_DISARMED, false,
                                     LED_PATTERN_DOUBLE_BLINK_SLOT_MS));
}

static void test_disarmed_uncalibrated_double_blink_second_slot_on(void)
{
    /* Arrange: slot 2 (300..449 ms) is the second short blink. */
    /* Act + Assert */
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_DISARMED, false,
                                    LED_PATTERN_DOUBLE_BLINK_SLOT_MS * 2));
}

static void test_uncalibrated_overlay_differs_from_calibrated(void)
{
    /* Arrange: in the gap slot the calibrated pattern is on (slow blink first
     * half) while the uncalibrated overlay is off -> distinguishable. */
    uint32_t t = LED_PATTERN_DOUBLE_BLINK_SLOT_MS; /* slot 1 gap */

    /* Act + Assert: the overlay only applies when !calibrated. */
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_DISARMED, true, t));
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_DISARMED, false, t));
}

/* --- FAILSAFE: fast 5 Hz, independent of previous state / calibrated --- */

static void test_failsafe_fast_blink_on_in_first_half(void)
{
    /* Act + Assert: first half of the 200 ms fast period is on. */
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_FAILSAFE, true, 0));
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_FAILSAFE, false, 99));
}

static void test_failsafe_fast_blink_off_in_second_half(void)
{
    /* Act + Assert: second half of the 200 ms fast period is off. */
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_FAILSAFE, true, 100));
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_FAILSAFE, false, 199));
}

static void test_failsafe_independent_of_calibrated(void)
{
    /* Act + Assert: FAILSAFE pattern is the same whether calibrated or not. */
    for (uint32_t t = 0; t < 400; t += 37) {
        TEST_ASSERT_EQUAL(led_pattern_on(SM_STATE_FAILSAFE, true, t),
                          led_pattern_on(SM_STATE_FAILSAFE, false, t));
    }
}

/* --- ESC_CALIBRATION: double-blink, independent of calibrated --- */

static void test_calibration_double_blink_first_slot_on(void)
{
    /* Act + Assert: slot 0 of the calibration period is the first blink. */
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_ESC_CALIBRATION, false, 0));
}

static void test_calibration_double_blink_gap_off(void)
{
    /* Act + Assert: slot 1 (gap) is off. */
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_ESC_CALIBRATION, false,
                                     LED_PATTERN_DOUBLE_BLINK_SLOT_MS));
}

static void test_calibration_idle_tail_is_off(void)
{
    /* Arrange: after the 4-slot double-blink window, the rest of the
     * calibration period is off. */
    uint32_t t = LED_PATTERN_DOUBLE_BLINK_SLOT_MS * 5;

    /* Act + Assert */
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_ESC_CALIBRATION, true, t));
}

/* --- DEPLOY: triple-blink burst --- */

static void test_deploy_first_blink_slot_on(void)
{
    /* The first of three blinks (slot 0). */
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_DEPLOY, true, 0));
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_DEPLOY, true,
                                    LED_PATTERN_DOUBLE_BLINK_SLOT_MS - 1U));
}

static void test_deploy_third_blink_slot_on(void)
{
    /* The third blink lands in slot 4 (3*150..). This slot is the signature of
     * DEPLOY: neither the DISARMED overlay nor ESC_CALIBRATION blink there. */
    uint32_t t = LED_PATTERN_DOUBLE_BLINK_SLOT_MS * 4U;
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_DEPLOY, true, t));
}

static void test_deploy_tail_is_off(void)
{
    /* After the 6-slot burst window the rest of the 2 s period is off. */
    uint32_t t = LED_PATTERN_DOUBLE_BLINK_SLOT_MS * 6U;
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_DEPLOY, true, t));
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_DEPLOY, true,
                                     LED_PATTERN_DEPLOY_PERIOD_MS - 1U));
}

static void test_deploy_third_blink_distinguishes_from_calibration(void)
{
    /* Oracle: at the third-blink slot (slot 4) DEPLOY is ON while the look-alike
     * ESC_CALIBRATION double-blink (only slots 0 and 2) is OFF, so the third
     * blink unambiguously marks DEPLOY. */
    uint32_t t = LED_PATTERN_DOUBLE_BLINK_SLOT_MS * 4U;
    TEST_ASSERT_TRUE(led_pattern_on(SM_STATE_DEPLOY, false, t));
    TEST_ASSERT_FALSE(led_pattern_on(SM_STATE_ESC_CALIBRATION, false, t));
}

/* Sample a state's waveform across its period into a coarse on/off signature. */
static int waveforms_differ(sm_state a, sm_state b, bool calibrated)
{
    for (uint32_t t = 0; t < LED_PATTERN_DEPLOY_PERIOD_MS; t += 25U) {
        if (led_pattern_on(a, calibrated, t) != led_pattern_on(b, calibrated, t)) {
            return 1;
        }
    }
    return 0;
}

static void test_deploy_waveform_differs_from_every_other_state(void)
{
    /* Oracle: DEPLOY's blink signature is not identical to ARMED, DISARMED,
     * FAILSAFE or ESC_CALIBRATION over a full period, so it is distinguishable. */
    TEST_ASSERT_TRUE(waveforms_differ(SM_STATE_DEPLOY, SM_STATE_ARMED, false));
    TEST_ASSERT_TRUE(waveforms_differ(SM_STATE_DEPLOY, SM_STATE_DISARMED, false));
    TEST_ASSERT_TRUE(waveforms_differ(SM_STATE_DEPLOY, SM_STATE_DISARMED, true));
    TEST_ASSERT_TRUE(waveforms_differ(SM_STATE_DEPLOY, SM_STATE_FAILSAFE, false));
    TEST_ASSERT_TRUE(
        waveforms_differ(SM_STATE_DEPLOY, SM_STATE_ESC_CALIBRATION, false));
}

static void test_deploy_independent_of_calibrated(void)
{
    /* The calibrated flag only affects DISARMED; DEPLOY ignores it. */
    for (uint32_t t = 0; t < LED_PATTERN_DEPLOY_PERIOD_MS; t += 50U) {
        TEST_ASSERT_EQUAL(led_pattern_on(SM_STATE_DEPLOY, true, t),
                          led_pattern_on(SM_STATE_DEPLOY, false, t));
    }
}

/* --- RGB colour mapping at 25% brightness (255 -> 63, 160 -> 40) --- */

static void test_armed_colour_is_green(void)
{
    /* ARMED is solid on -> green scaled to 25%. */
    LedColor c = led_pattern_color(SM_STATE_ARMED, true, 0);
    TEST_ASSERT_EQUAL_UINT8(0U, c.r);
    TEST_ASSERT_EQUAL_UINT8(63U, c.g);
    TEST_ASSERT_EQUAL_UINT8(0U, c.b);
}

static void test_disarmed_colour_is_amber_on_phase(void)
{
    /* DISARMED on-half (t=0) -> amber. */
    LedColor c = led_pattern_color(SM_STATE_DISARMED, true, 0);
    TEST_ASSERT_EQUAL_UINT8(63U, c.r);
    TEST_ASSERT_EQUAL_UINT8(40U, c.g);
    TEST_ASSERT_EQUAL_UINT8(0U, c.b);
}

static void test_failsafe_colour_is_red_on_phase(void)
{
    /* FAILSAFE on-half (t=0 in the 200 ms period) -> red. */
    LedColor c = led_pattern_color(SM_STATE_FAILSAFE, false, 0);
    TEST_ASSERT_EQUAL_UINT8(63U, c.r);
    TEST_ASSERT_EQUAL_UINT8(0U, c.g);
    TEST_ASSERT_EQUAL_UINT8(0U, c.b);
}

static void test_calibration_colour_is_blue_on_phase(void)
{
    /* ESC_CALIBRATION first blink slot (t=0) is on -> blue. */
    LedColor c = led_pattern_color(SM_STATE_ESC_CALIBRATION, false, 0);
    TEST_ASSERT_EQUAL_UINT8(0U, c.r);
    TEST_ASSERT_EQUAL_UINT8(0U, c.g);
    TEST_ASSERT_EQUAL_UINT8(63U, c.b);
}

static void test_deploy_colour_is_cyan_on_phase(void)
{
    /* DEPLOY first blink slot (t=0) is on -> cyan. */
    LedColor c = led_pattern_color(SM_STATE_DEPLOY, true, 0);
    TEST_ASSERT_EQUAL_UINT8(0U, c.r);
    TEST_ASSERT_EQUAL_UINT8(63U, c.g);
    TEST_ASSERT_EQUAL_UINT8(63U, c.b);
}

static void test_off_phase_is_black(void)
{
    /* FAILSAFE off-half (t=100 in the 200 ms period) -> LED dark, colour gone. */
    LedColor c = led_pattern_color(SM_STATE_FAILSAFE, false, 100);
    TEST_ASSERT_EQUAL_UINT8(0U, c.r);
    TEST_ASSERT_EQUAL_UINT8(0U, c.g);
    TEST_ASSERT_EQUAL_UINT8(0U, c.b);
}

static void test_brightness_capped_at_quarter(void)
{
    /* Oracle for the 25% spec: the brightest channel of any solid state is 63
     * (255 * 25 / 100), never full 255 — fails if brightness scaling is lost. */
    LedColor armed = led_pattern_color(SM_STATE_ARMED, true, 0);
    TEST_ASSERT_EQUAL_UINT8(63U, armed.g);
    TEST_ASSERT_TRUE(armed.g < 255U);
}

void run_led_pattern_tests(void)
{
    RUN_TEST(test_armed_is_solid_on_at_any_time);
    RUN_TEST(test_disarmed_calibrated_on_in_first_half);
    RUN_TEST(test_disarmed_calibrated_off_in_second_half);
    RUN_TEST(test_disarmed_calibrated_period_repeats);
    RUN_TEST(test_disarmed_uncalibrated_double_blink_first_slot_on);
    RUN_TEST(test_disarmed_uncalibrated_double_blink_gap_off);
    RUN_TEST(test_disarmed_uncalibrated_double_blink_second_slot_on);
    RUN_TEST(test_uncalibrated_overlay_differs_from_calibrated);
    RUN_TEST(test_failsafe_fast_blink_on_in_first_half);
    RUN_TEST(test_failsafe_fast_blink_off_in_second_half);
    RUN_TEST(test_failsafe_independent_of_calibrated);
    RUN_TEST(test_calibration_double_blink_first_slot_on);
    RUN_TEST(test_calibration_double_blink_gap_off);
    RUN_TEST(test_calibration_idle_tail_is_off);
    RUN_TEST(test_deploy_first_blink_slot_on);
    RUN_TEST(test_deploy_third_blink_slot_on);
    RUN_TEST(test_deploy_tail_is_off);
    RUN_TEST(test_deploy_third_blink_distinguishes_from_calibration);
    RUN_TEST(test_deploy_waveform_differs_from_every_other_state);
    RUN_TEST(test_deploy_independent_of_calibrated);
    RUN_TEST(test_armed_colour_is_green);
    RUN_TEST(test_disarmed_colour_is_amber_on_phase);
    RUN_TEST(test_failsafe_colour_is_red_on_phase);
    RUN_TEST(test_calibration_colour_is_blue_on_phase);
    RUN_TEST(test_deploy_colour_is_cyan_on_phase);
    RUN_TEST(test_off_phase_is_black);
    RUN_TEST(test_brightness_capped_at_quarter);
}
