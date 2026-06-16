#include "esc_calibration.h"
#include "unity.h"

/* The constant pulse widths the sequence emits per step (the oracle: the values
 * an operator hears the WP880 confirm during range learn). */
#define NEUTRAL_US 1500U
#define FORWARD_US 2000U
#define REVERSE_US 1000U

/* A nominal "running" calibration input: RC valid, no event, no timeout. Tests
 * flip individual fields to exercise one rule. */
static calib_inputs running_at(calib_step step)
{
    calib_inputs in = {
        .step = step,
        .event = CALIB_EVENT_NONE,
        .rc_valid = true,
        .timeout = false,
    };
    return in;
}

/* --- Step -> constant mapping --- */

static void test_step_constants_map_to_expected_us(void)
{
    /* Arrange / Act / Assert: the fixed range-learn constants. */
    TEST_ASSERT_EQUAL_UINT32(NEUTRAL_US, calib_us_for_step(CALIB_STEP_NEUTRAL));
    TEST_ASSERT_EQUAL_UINT32(FORWARD_US, calib_us_for_step(CALIB_STEP_FORWARD));
    TEST_ASSERT_EQUAL_UINT32(REVERSE_US, calib_us_for_step(CALIB_STEP_REVERSE));
    TEST_ASSERT_EQUAL_UINT32(NEUTRAL_US, calib_us_for_step(CALIB_STEP_DONE));
    TEST_ASSERT_EQUAL_UINT32(NEUTRAL_US, calib_neutral_us());
}

/* --- Holding a step (no event) keeps it and emits its constant --- */

static void test_neutral_holds_and_emits_1500(void)
{
    calib_inputs in = running_at(CALIB_STEP_NEUTRAL);

    calib_outputs out = calib_step_next(&in);

    TEST_ASSERT_EQUAL(CALIB_EXIT_NONE, out.exit);
    TEST_ASSERT_EQUAL(CALIB_STEP_NEUTRAL, out.step);
    TEST_ASSERT_EQUAL_UINT32(NEUTRAL_US, out.esc_us);
}

/* --- Advancing through the sequence: 1500 -> 2000 -> 1000 -> Done --- */

static void test_next_neutral_to_forward_emits_2000(void)
{
    calib_inputs in = running_at(CALIB_STEP_NEUTRAL);
    in.event = CALIB_EVENT_NEXT;

    calib_outputs out = calib_step_next(&in);

    TEST_ASSERT_EQUAL(CALIB_EXIT_NONE, out.exit);
    TEST_ASSERT_EQUAL(CALIB_STEP_FORWARD, out.step);
    TEST_ASSERT_EQUAL_UINT32(FORWARD_US, out.esc_us);
}

static void test_next_forward_to_reverse_emits_1000(void)
{
    calib_inputs in = running_at(CALIB_STEP_FORWARD);
    in.event = CALIB_EVENT_NEXT;

    calib_outputs out = calib_step_next(&in);

    TEST_ASSERT_EQUAL(CALIB_EXIT_NONE, out.exit);
    TEST_ASSERT_EQUAL(CALIB_STEP_REVERSE, out.step);
    TEST_ASSERT_EQUAL_UINT32(REVERSE_US, out.esc_us);
}

static void test_next_reverse_completes_to_disarmed_neutral(void)
{
    /* Advancing past the last controllable step (REVERSE) finishes the
     * sequence: Done -> exit to DISARMED with the ESC parked at neutral. */
    calib_inputs in = running_at(CALIB_STEP_REVERSE);
    in.event = CALIB_EVENT_NEXT;

    calib_outputs out = calib_step_next(&in);

    TEST_ASSERT_EQUAL(CALIB_EXIT_TO_DISARMED, out.exit);
    TEST_ASSERT_EQUAL_UINT32(NEUTRAL_US, out.esc_us);
}

/* --- Aborts --- */

static void test_rc_invalid_aborts_to_failsafe(void)
{
    /* RC loss dominates every other input, at any step. */
    calib_inputs in = running_at(CALIB_STEP_FORWARD);
    in.rc_valid = false;
    in.event = CALIB_EVENT_NEXT; /* even a pending advance is overridden */

    calib_outputs out = calib_step_next(&in);

    TEST_ASSERT_EQUAL(CALIB_EXIT_TO_FAILSAFE, out.exit);
    TEST_ASSERT_EQUAL_UINT32(NEUTRAL_US, out.esc_us);
}

static void test_timeout_aborts_to_disarmed_neutral(void)
{
    calib_inputs in = running_at(CALIB_STEP_FORWARD);
    in.timeout = true;

    calib_outputs out = calib_step_next(&in);

    TEST_ASSERT_EQUAL(CALIB_EXIT_TO_DISARMED, out.exit);
    TEST_ASSERT_EQUAL_UINT32(NEUTRAL_US, out.esc_us);
}

static void test_cancel_aborts_to_disarmed_neutral(void)
{
    calib_inputs in = running_at(CALIB_STEP_REVERSE);
    in.event = CALIB_EVENT_CANCEL;

    calib_outputs out = calib_step_next(&in);

    TEST_ASSERT_EQUAL(CALIB_EXIT_TO_DISARMED, out.exit);
    TEST_ASSERT_EQUAL_UINT32(NEUTRAL_US, out.esc_us);
}

static void test_rc_loss_takes_precedence_over_cancel_and_timeout(void)
{
    /* Boundary: when RC loss and a DISARMED-bound abort fire the same cycle,
     * RC loss wins (FAILSAFE is the safer latch). */
    calib_inputs in = running_at(CALIB_STEP_NEUTRAL);
    in.rc_valid = false;
    in.timeout = true;
    in.event = CALIB_EVENT_CANCEL;

    calib_outputs out = calib_step_next(&in);

    TEST_ASSERT_EQUAL(CALIB_EXIT_TO_FAILSAFE, out.exit);
}

void run_esc_calibration_tests(void)
{
    RUN_TEST(test_step_constants_map_to_expected_us);
    RUN_TEST(test_neutral_holds_and_emits_1500);
    RUN_TEST(test_next_neutral_to_forward_emits_2000);
    RUN_TEST(test_next_forward_to_reverse_emits_1000);
    RUN_TEST(test_next_reverse_completes_to_disarmed_neutral);
    RUN_TEST(test_rc_invalid_aborts_to_failsafe);
    RUN_TEST(test_timeout_aborts_to_disarmed_neutral);
    RUN_TEST(test_cancel_aborts_to_disarmed_neutral);
    RUN_TEST(test_rc_loss_takes_precedence_over_cancel_and_timeout);
}
