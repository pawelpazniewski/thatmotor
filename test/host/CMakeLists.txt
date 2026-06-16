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
)

set(PURE_INCLUDE_DIRS
    ${REPO_ROOT}/components/safety_clamp/include
    ${REPO_ROOT}/components/pwm_out/include
)

# Test sources.
set(TEST_SOURCES
    test_main.c
    test_safety_clamp.c
    test_pwm_us_to_duty.c
    test_pwm_out_logic.c
)

add_executable(host_tests ${TEST_SOURCES} ${PURE_SOURCES})
target_include_directories(host_tests PRIVATE ${PURE_INCLUDE_DIRS})
target_link_libraries(host_tests PRIVATE unity)
