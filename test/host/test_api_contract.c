#include <string.h>

#include "api_contract.h"
#include "unity.h"

/* --- Success envelope: { data, error:null } --- */

static void test_success_with_data_has_data_and_null_error(void)
{
    /* Arrange */
    char buf[128];

    /* Act */
    size_t n = api_build_success("{\"max_throttle_pct\":40}", buf, sizeof(buf));

    /* Assert: data populated, error explicitly null. */
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"data\":{\"max_throttle_pct\":40},\"error\":null}", buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"error\":null"));
    TEST_ASSERT_NULL(strstr(buf, "\"code\""));
}

static void test_success_without_data_emits_data_null(void)
{
    /* Arrange */
    char buf[64];

    /* Act */
    size_t n = api_build_success(NULL, buf, sizeof(buf));

    /* Assert: both data and error null on a contentless success. */
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_EQUAL_STRING("{\"data\":null,\"error\":null}", buf);
}

/* --- Validation failure: error.code/message, no data --- */

static void test_validation_failure_has_error_code_and_message_no_data(void)
{
    /* Arrange */
    char buf[256];

    /* Act */
    size_t n = api_build_error(API_ERR_VALIDATION_FAILED, NULL, buf, sizeof(buf));

    /* Assert: data is null, error has code+message. */
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"data\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"code\":\"VALIDATION_FAILED\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"message\":\""));
    /* No data object is present on a failure. */
    TEST_ASSERT_NULL(strstr(buf, "\"data\":{"));
}

static void test_error_uses_custom_message_when_provided(void)
{
    /* Arrange */
    char buf[256];

    /* Act */
    size_t n = api_build_error(API_ERR_VALIDATION_FAILED,
                               "max_throttle_pct out of range", buf, sizeof(buf));

    /* Assert: caller message wins over the default. */
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "max_throttle_pct out of range"));
}

/* --- Write rejected in ARMED -> SETTINGS_WRITE_REJECTED_NOT_DISARMED --- */

static void test_not_disarmed_uses_settings_write_rejected_code(void)
{
    /* Arrange */
    char buf[256];

    /* Act: a write attempt while not DISARMED maps to the contract code. */
    size_t n = api_build_error(API_ERR_NOT_DISARMED, NULL, buf, sizeof(buf));

    /* Assert: the exact stable code is present, no data. */
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_NOT_NULL(
        strstr(buf, "\"code\":\"SETTINGS_WRITE_REJECTED_NOT_DISARMED\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"data\":null"));
}

static void test_not_disarmed_code_string_is_stable(void)
{
    /* Act + Assert: the code string is the documented contract constant. */
    TEST_ASSERT_EQUAL_STRING("SETTINGS_WRITE_REJECTED_NOT_DISARMED",
                             api_error_code_str(API_ERR_NOT_DISARMED));
    TEST_ASSERT_EQUAL_STRING(API_CODE_NOT_DISARMED,
                             api_error_code_str(API_ERR_NOT_DISARMED));
}

/* --- JSON escaping + buffer safety --- */

static void test_error_message_with_quotes_is_escaped(void)
{
    /* Arrange */
    char buf[256];

    /* Act: a message containing a quote must be escaped, not break the JSON. */
    size_t n = api_build_error(API_ERR_BAD_REQUEST, "bad \"field\"", buf,
                               sizeof(buf));

    /* Assert: the quote is escaped in the output. */
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "bad \\\"field\\\""));
}

static void test_too_small_buffer_returns_zero_and_terminates(void)
{
    /* Arrange: a buffer far too small for the envelope. */
    char buf[8];

    /* Act */
    size_t n = api_build_error(API_ERR_NOT_DISARMED, NULL, buf, sizeof(buf));

    /* Assert: signals failure (0) and leaves a NUL-terminated buffer. */
    TEST_ASSERT_EQUAL_UINT(0, n);
    TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
}

void run_api_contract_tests(void)
{
    RUN_TEST(test_success_with_data_has_data_and_null_error);
    RUN_TEST(test_success_without_data_emits_data_null);
    RUN_TEST(test_validation_failure_has_error_code_and_message_no_data);
    RUN_TEST(test_error_uses_custom_message_when_provided);
    RUN_TEST(test_not_disarmed_uses_settings_write_rejected_code);
    RUN_TEST(test_not_disarmed_code_string_is_stable);
    RUN_TEST(test_error_message_with_quotes_is_escaped);
    RUN_TEST(test_too_small_buffer_returns_zero_and_terminates);
}
