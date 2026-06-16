# Standalone host test harness for pure-logic firmware components.
# Builds Unity + the framework-agnostic component sources + test files into a
# single executable. Requires only cmake/ninja + a host C compiler (no ESP-IDF).
cmake_minimum_required(VERSION 3.16)
project(kayak_motor_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Vendored Unity (copied from $IDF_PATH/components/unity/unity/src).
add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC unity)

# Pure component sources under test. Only framework-agnostic files belong here.
set(PURE_SOURCES
    ${REPO_ROOT}/components/safety_clamp/src/safety_clamp.c
    ${REPO_ROOT}/components/pwm_out/src/pwm_us_to_duty.c
    ${REPO_ROOT}/components/pwm_out/src/pwm_out_logic.c
    ${REPO_ROOT}/components/rc_capture/src/cap_math.c
    ${REPO_ROOT}/components/rc_validity/src/rc_validity.c
    ${REPO_ROOT}/components/settings/src/settings_defaults.c
    ${REPO_ROOT}/components/settings/src/settings_validate.c
    ${REPO_ROOT}/components/settings/src/blob_codec.c
    ${REPO_ROOT}/components/settings/src/commit_debounce.c
    ${REPO_ROOT}/components/signal_chain/src/ramp.c
    ${REPO_ROOT}/components/signal_chain/src/chain_math.c
    ${REPO_ROOT}/components/signal_chain/src/throttle_chain.c
    ${REPO_ROOT}/components/signal_chain/src/servo_chain.c
    ${REPO_ROOT}/components/state_machine/src/state_machine.c
    ${REPO_ROOT}/components/control_loop/src/loop_step.c
)

set(PURE_INCLUDE_DIRS
    ${REPO_ROOT}/components/safety_clamp/include
    ${REPO_ROOT}/components/pwm_out/include
    ${REPO_ROOT}/components/rc_capture/include
    ${REPO_ROOT}/components/rc_validity/include
    ${REPO_ROOT}/components/settings/include
    ${REPO_ROOT}/components/settings/src
    ${REPO_ROOT}/components/signal_chain/include
    ${REPO_ROOT}/components/signal_chain/src
    ${REPO_ROOT}/components/state_machine/include
    ${REPO_ROOT}/components/control_loop/include
)

# Test sources.
set(TEST_SOURCES
    test_main.c
    test_safety_clamp.c
    test_pwm_us_to_duty.c
    test_pwm_out_logic.c
    test_cap_math.c
    test_rc_validity.c
    test_settings_validate.c
    test_blob_codec.c
    test_commit_debounce.c
    test_ramp.c
    test_throttle_chain.c
    test_servo_chain.c
    test_state_machine.c
    test_loop_step.c
)

add_executable(host_tests ${TEST_SOURCES} ${PURE_SOURCES})
target_include_directories(host_tests PRIVATE ${PURE_INCLUDE_DIRS})
target_link_libraries(host_tests PRIVATE unity)
