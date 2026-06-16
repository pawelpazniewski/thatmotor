#include "api_contract.h"
#include "params_decide.h"
#include "state_machine.h"
#include "unity.h"

/* --- ARMED -> reject not-disarmed (the headline 409 scenario) --- */

static void test_armed_valid_rejects_not_disarmed(void)
{
    /* Act: a valid write attempted while ARMED. */
    params_write_outcome o = params_decide_write(SM_STATE_ARMED, true);

    /* Assert: rejected as not-DISARMED with the contract 409 code. */
    TEST_ASSERT_EQUAL_INT(PARAMS_WRITE_REJECT_NOT_DISARMED, o.decision);
    TEST_ASSERT_EQUAL_INT(API_ERR_NOT_DISARMED, o.code);
    TEST_ASSERT_EQUAL_INT(409, o.http_status);
    TEST_ASSERT_EQUAL_STRING(API_CODE_NOT_DISARMED,
                             api_error_code_str(o.code));
}

static void test_armed_invalid_still_rejects_not_disarmed(void)
{
    /* State gate has precedence over field validity. */
    params_write_outcome o = params_decide_write(SM_STATE_ARMED, false);

    TEST_ASSERT_EQUAL_INT(PARAMS_WRITE_REJECT_NOT_DISARMED, o.decision);
    TEST_ASSERT_EQUAL_INT(409, o.http_status);
}

static void test_failsafe_rejects_not_disarmed(void)
{
    params_write_outcome o = params_decide_write(SM_STATE_FAILSAFE, true);

    TEST_ASSERT_EQUAL_INT(PARAMS_WRITE_REJECT_NOT_DISARMED, o.decision);
}

/* --- DISARMED + out-of-range field -> reject invalid (400) --- */

static void test_disarmed_invalid_rejects_validation_failed(void)
{
    /* Act: DISARMED but a field failed re-validation. */
    params_write_outcome o = params_decide_write(SM_STATE_DISARMED, false);

    /* Assert: rejected as invalid with the contract 400 code. */
    TEST_ASSERT_EQUAL_INT(PARAMS_WRITE_REJECT_INVALID, o.decision);
    TEST_ASSERT_EQUAL_INT(API_ERR_VALIDATION_FAILED, o.code);
    TEST_ASSERT_EQUAL_INT(400, o.http_status);
}

/* --- DISARMED + all fields valid -> accept (200, staged) --- */

static void test_disarmed_valid_accepts(void)
{
    /* Act: DISARMED and every field valid. */
    params_write_outcome o = params_decide_write(SM_STATE_DISARMED, true);

    /* Assert: accepted; no error code, 200. */
    TEST_ASSERT_EQUAL_INT(PARAMS_WRITE_ACCEPT, o.decision);
    TEST_ASSERT_EQUAL_INT(API_OK, o.code);
    TEST_ASSERT_EQUAL_INT(200, o.http_status);
}

void run_params_decide_tests(void)
{
    RUN_TEST(test_armed_valid_rejects_not_disarmed);
    RUN_TEST(test_armed_invalid_still_rejects_not_disarmed);
    RUN_TEST(test_failsafe_rejects_not_disarmed);
    RUN_TEST(test_disarmed_invalid_rejects_validation_failed);
    RUN_TEST(test_disarmed_valid_accepts);
}
