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
void run_ch4_switch_tests(void);
void run_settings_validate_tests(void);
void run_blob_codec_tests(void);
void run_commit_debounce_tests(void);
void run_nvs_provenance_tests(void);
void run_ramp_tests(void);
void run_throttle_chain_tests(void);
void run_servo_chain_tests(void);
void run_state_machine_tests(void);
void run_esc_calibration_tests(void);
void run_loop_step_tests(void);
void run_api_contract_tests(void);
void run_command_parse_tests(void);
void run_params_decide_tests(void);
void run_wifi_ap_config_tests(void);
void run_led_pattern_tests(void);

int main(void)
{
    UNITY_BEGIN();
    run_safety_clamp_tests();
    run_pwm_us_to_duty_tests();
    run_pwm_out_logic_tests();
    run_cap_math_tests();
    run_rc_validity_tests();
    run_ch4_switch_tests();
    run_settings_validate_tests();
    run_blob_codec_tests();
    run_commit_debounce_tests();
    run_nvs_provenance_tests();
    run_ramp_tests();
    run_throttle_chain_tests();
    run_servo_chain_tests();
    run_state_machine_tests();
    run_esc_calibration_tests();
    run_loop_step_tests();
    run_api_contract_tests();
    run_command_parse_tests();
    run_params_decide_tests();
    run_wifi_ap_config_tests();
    run_led_pattern_tests();
    return UNITY_END();
}
