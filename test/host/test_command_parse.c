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

static void test_deploy_maps_to_deploy_request(void)
{
    command_parse_result r = command_parse("deploy");

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.deploy_request);
    TEST_ASSERT_FALSE(r.arm_request);
    TEST_ASSERT_FALSE(r.stow_request);
}

static void test_stow_maps_to_stow_request(void)
{
    command_parse_result r = command_parse("stow");

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.stow_request);
    TEST_ASSERT_FALSE(r.deploy_request);
    TEST_ASSERT_FALSE(r.disarm_request);
}

static void test_trim_left_maps_to_trim_left(void)
{
    command_parse_result r = command_parse("trim_left");

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.trim_left);
    TEST_ASSERT_FALSE(r.trim_right);
    TEST_ASSERT_FALSE(r.trim_save);
}

static void test_trim_right_maps_to_trim_right(void)
{
    command_parse_result r = command_parse("trim_right");

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.trim_right);
    TEST_ASSERT_FALSE(r.trim_left);
    TEST_ASSERT_FALSE(r.trim_save);
}

static void test_trim_save_maps_to_trim_save(void)
{
    command_parse_result r = command_parse("trim_save");

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.trim_save);
    TEST_ASSERT_FALSE(r.trim_left);
    TEST_ASSERT_FALSE(r.trim_right);
}

static void test_goto_maps_to_goto_request(void)
{
    command_parse_result r = command_parse("goto");

    /* Keyword-only: flag set, lat/lon left for the HTTP layer to inject. */
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.goto_request);
    TEST_ASSERT_FALSE(r.goto_cancel_request);
    TEST_ASSERT_EQUAL_INT32(0, r.goto_lat_e7);
    TEST_ASSERT_EQUAL_INT32(0, r.goto_lon_e7);
}

static void test_goto_cancel_maps_to_goto_cancel_request(void)
{
    command_parse_result r = command_parse("goto_cancel");

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.goto_cancel_request);
    TEST_ASSERT_FALSE(r.goto_request);
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
    RUN_TEST(test_deploy_maps_to_deploy_request);
    RUN_TEST(test_stow_maps_to_stow_request);
    RUN_TEST(test_calib_start_sets_request_and_confirm);
    RUN_TEST(test_calib_next_maps_to_event_next);
    RUN_TEST(test_calib_cancel_maps_to_event_cancel);
    RUN_TEST(test_trim_left_maps_to_trim_left);
    RUN_TEST(test_trim_right_maps_to_trim_right);
    RUN_TEST(test_trim_save_maps_to_trim_save);
    RUN_TEST(test_goto_maps_to_goto_request);
    RUN_TEST(test_goto_cancel_maps_to_goto_cancel_request);
    RUN_TEST(test_unknown_keyword_is_rejected);
    RUN_TEST(test_substring_of_known_keyword_is_rejected);
    RUN_TEST(test_null_keyword_is_rejected);
    RUN_TEST(test_empty_keyword_is_rejected);
}
