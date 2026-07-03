#include <stdbool.h>

#include "blackbox_reason.h"
#include "unity.h"

/* Field order: (is_failsafe, is_armed, sticks_neutral, ch3_high, was_goto). */

static void test_failsafe_outranks_everything(void)
{
    /* Even with a stick moved and CH3 high, a lost-RC failsafe is THE reason. */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_END_FAILSAFE,
        blackbox_end_reason_decide(true, false, false, true, true));
}

static void test_disarm_when_not_armed_and_not_failsafe(void)
{
    /* Not failsafe, not armed -> disarm, regardless of stick/CH3/goto. */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_END_DISARM,
        blackbox_end_reason_decide(false, false, false, true, true));
}

static void test_stick_override_when_armed_and_sticks_moved(void)
{
    /* Armed, sticks off neutral -> manual stick override, even over CH3/goto. */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_END_STICK,
        blackbox_end_reason_decide(false, true, false, true, true));
}

static void test_ch3_when_armed_sticks_neutral_ch3_high(void)
{
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_END_CH3,
        blackbox_end_reason_decide(false, true, true, true, true));
}

static void test_goto_cancel_when_only_goto_was_owner(void)
{
    /* Armed, sticks neutral, CH3 low, and the dropped session was goto. */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_END_GOTO_CANCEL,
        blackbox_end_reason_decide(false, true, true, false, true));
}

static void test_other_when_armed_and_no_specific_cause(void)
{
    /* Armed, quiet sticks, CH3 low, not a goto session: undetermined. */
    TEST_ASSERT_EQUAL_INT(
        BLACKBOX_END_OTHER,
        blackbox_end_reason_decide(false, true, true, false, false));
}

void run_blackbox_reason_tests(void)
{
    RUN_TEST(test_failsafe_outranks_everything);
    RUN_TEST(test_disarm_when_not_armed_and_not_failsafe);
    RUN_TEST(test_stick_override_when_armed_and_sticks_moved);
    RUN_TEST(test_ch3_when_armed_sticks_neutral_ch3_high);
    RUN_TEST(test_goto_cancel_when_only_goto_was_owner);
    RUN_TEST(test_other_when_armed_and_no_specific_cause);
}
