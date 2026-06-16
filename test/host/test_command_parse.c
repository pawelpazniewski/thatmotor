#include "command_parse.h"
#include "unity.h"

/* --- Known keywords map to the matching event fields --- */

static void test_arm_maps_to_arm_request(void)
{
    /* Act */
    command_parse_result r = command_parse("arm");

    /* Assert: recognised, only arm_request set. */
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.arm_request);
    TEST_ASSERT_FALSE(r.disarm_request);
    TEST_ASSERT_FALSE(r.calib_request);
    TEST_ASSERT_EQUAL_INT(CALIB_EVENT_NONE, r.calib_event);
}

static void test_disarm_maps_to_disarm_request(void)
{
    command_parse_result r = command_parse("disarm");

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.disarm_request);
    TEST_ASSERT_FALSE(r.arm_request);
}

static void test_calib_start_sets_request_and_confirm(void)
{
    command_parse_result r = command_parse("calib_start");

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.calib_request);
    TEST_ASSERT_TRUE(r.calib_confirm);
    TEST_ASSERT_EQUAL_INT(CALIB_EVENT_NONE, r.calib_event);
}

static void test_calib_next_maps_to_event_next(void)
{
    command_parse_result r = command_parse("calib_next");

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(CALIB_EVENT_NEXT, r.calib_event);
    TEST_ASSERT_FALSE(r.calib_request);
}

static void test_calib_cancel_maps_to_event_cancel(void)
{
    command_parse_result r = command_parse("calib_cancel");

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(CALIB_EVENT_CANCEL, r.calib_event);
}

/* --- Unknown / malformed keywords are rejected (no substring match) --- */

static void test_unknown_keyword_is_rejected(void)
{
    command_parse_result r = command_parse("explode");

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.arm_request);
    TEST_ASSERT_FALSE(r.disarm_request);
}

static void test_substring_of_known_keyword_is_rejected(void)
{
    /* "do not arm" must NOT match "arm" (exact match, not substring). */
    command_parse_result r = command_parse("do not arm");

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.arm_request);
}

static void test_null_keyword_is_rejected(void)
{
    command_parse_result r = command_parse(NULL);

    TEST_ASSERT_FALSE(r.ok);
}

static void test_empty_keyword_is_rejected(void)
{
    command_parse_result r = command_parse("");

    TEST_ASSERT_FALSE(r.ok);
}

void run_command_parse_tests(void)
{
    RUN_TEST(test_arm_maps_to_arm_request);
    RUN_TEST(test_disarm_maps_to_disarm_request);
    RUN_TEST(test_calib_start_sets_request_and_confirm);
    RUN_TEST(test_calib_next_maps_to_event_next);
    RUN_TEST(test_calib_cancel_maps_to_event_cancel);
    RUN_TEST(test_unknown_keyword_is_rejected);
    RUN_TEST(test_substring_of_known_keyword_is_rejected);
    RUN_TEST(test_null_keyword_is_rejected);
    RUN_TEST(test_empty_keyword_is_rejected);
}
