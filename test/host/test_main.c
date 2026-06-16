#include "unity.h"

/* Unity requires setUp/tearDown to be defined by the test binary. */
void setUp(void) {}
void tearDown(void) {}

/* Test entry points defined in the per-component test files. */
void run_safety_clamp_tests(void);
void run_pwm_us_to_duty_tests(void);
void run_pwm_out_logic_tests(void);
void run_cap_math_tests(void);
void run_rc_validity_tests(void);
void run_settings_validate_tests(void);

int main(void)
{
    UNITY_BEGIN();
    run_safety_clamp_tests();
    run_pwm_us_to_duty_tests();
    run_pwm_out_logic_tests();
    run_cap_math_tests();
    run_rc_validity_tests();
    run_settings_validate_tests();
    return UNITY_END();
}
