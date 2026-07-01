#include <string.h>

#include "params_cmd.h"
#include "settings_model.h"
#include "settings_validate.h"
#include "state_machine.h"
#include "unity.h"

static settings_params defaults(void)
{
    settings_params p;
    settings_load_defaults(&p);
    return p;
}

/* ---- params set: happy path (in range) ---- */

static void test_set_in_range_accepts_and_stages(void)
{
    settings_params current = defaults();
    settings_params staged;

    /* throttle_gain range is [0, 1000]; 50 is comfortably inside. */
    params_cmd_set_outcome oc =
        params_cmd_decide_set(&current, "throttle_gain", "50", &staged);

    TEST_ASSERT_EQUAL_INT(PARAMS_CMD_SET_ACCEPT, oc);
    TEST_ASSERT_EQUAL_UINT16(50U, staged.spot_lock_throttle_gain);
    /* Only the addressed field changes; the others keep the active values. */
    TEST_ASSERT_EQUAL_UINT16(current.spot_lock_deadband_m,
                             staged.spot_lock_deadband_m);
    TEST_ASSERT_EQUAL_UINT16(current.spot_lock_servo_gain,
                             staged.spot_lock_servo_gain);
}

/* ---- params set: out of range (oracle power) ---- */

static void test_set_out_of_range_rejected_no_stage(void)
{
    settings_params current = defaults();
    settings_params staged = current; /* sentinel: must stay unchanged on reject */

    /* max_throttle_pct range is [0, 100]; 200 is out of range. A path that
     * skipped validation would ACCEPT this and stage a bad value. */
    params_cmd_set_outcome oc =
        params_cmd_decide_set(&current, "max_throttle_pct", "200", &staged);

    TEST_ASSERT_EQUAL_INT(PARAMS_CMD_SET_ERR_OUT_OF_RANGE, oc);
    TEST_ASSERT_NOT_EQUAL(PARAMS_CMD_SET_ACCEPT, oc);
}

static void test_set_unknown_field_rejected(void)
{
    settings_params current = defaults();
    settings_params staged;
    TEST_ASSERT_EQUAL_INT(
        PARAMS_CMD_SET_ERR_UNKNOWN_FIELD,
        params_cmd_decide_set(&current, "not_a_field", "5", &staged));
}

static void test_set_bad_value_rejected(void)
{
    settings_params current = defaults();
    settings_params staged;
    TEST_ASSERT_EQUAL_INT(
        PARAMS_CMD_SET_ERR_BAD_VALUE,
        params_cmd_decide_set(&current, "deadband_m", "12x", &staged));
    /* Overflow past u16 is a bad value too. */
    TEST_ASSERT_EQUAL_INT(
        PARAMS_CMD_SET_ERR_BAD_VALUE,
        params_cmd_decide_set(&current, "deadband_m", "70000", &staged));
}

static void test_set_null_args_rejected(void)
{
    settings_params current = defaults();
    settings_params staged;
    TEST_ASSERT_EQUAL_INT(
        PARAMS_CMD_SET_ERR_ARG,
        params_cmd_decide_set(NULL, "deadband_m", "3", &staged));
    TEST_ASSERT_EQUAL_INT(
        PARAMS_CMD_SET_ERR_ARG,
        params_cmd_decide_set(&current, NULL, "3", &staged));
}

/* ---- params get: reflects current active settings ---- */

static void test_get_reflects_current_values(void)
{
    settings_params p = defaults();
    p.spot_lock_deadband_m = 7U;
    p.spot_lock_max_throttle_pct = 42U;
    p.spot_lock_throttle_gain = 88U;
    p.spot_lock_servo_gain = 15U;

    char buf[PARAMS_CMD_GET_MAX];
    size_t n = params_cmd_format_get(&p, buf, sizeof(buf));

    TEST_ASSERT_TRUE(n > 0U);
    TEST_ASSERT_NOT_NULL(strstr(buf, "deadband_m=7\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "max_throttle_pct=42\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "throttle_gain=88\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "servo_gain=15\n"));
}

/* ---- SI-6 gate: staged in ARMED, applies on DISARM ---- */

static void test_apply_gate_matches_si6(void)
{
    /* DISARMED applies immediately; every other state stages until disarm (the
     * loop's maybe_apply_pending gate). A change made while ARMED is never
     * dropped, it just waits. */
    TEST_ASSERT_EQUAL_INT(PARAMS_CMD_APPLIED,
                          params_cmd_apply_when(SM_STATE_DISARMED));
    TEST_ASSERT_EQUAL_INT(PARAMS_CMD_STAGED,
                          params_cmd_apply_when(SM_STATE_ARMED));
    TEST_ASSERT_EQUAL_INT(PARAMS_CMD_STAGED,
                          params_cmd_apply_when(SM_STATE_FAILSAFE));
    TEST_ASSERT_EQUAL_INT(PARAMS_CMD_STAGED,
                          params_cmd_apply_when(SM_STATE_DEPLOY));
}

void run_params_cmd_tests(void)
{
    RUN_TEST(test_set_in_range_accepts_and_stages);
    RUN_TEST(test_set_out_of_range_rejected_no_stage);
    RUN_TEST(test_set_unknown_field_rejected);
    RUN_TEST(test_set_bad_value_rejected);
    RUN_TEST(test_set_null_args_rejected);
    RUN_TEST(test_get_reflects_current_values);
    RUN_TEST(test_apply_gate_matches_si6);
}
