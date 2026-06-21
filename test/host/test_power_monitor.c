#include "power_monitor.h"
#include "unity.h"

static void test_power_monitor_short_sag_does_not_warn(void)
{
    power_monitor_cfg cfg = power_monitor_default_cfg();
    cfg.warn_mv = 12000U;
    cfg.clear_mv = 12300U;
    cfg.warn_frames = 5U;
    cfg.filtered_shift = 0U; /* isolate warning debounce from EMA behaviour */

    power_monitor_state state;
    power_monitor_init(&state);
    power_monitor_update(&state, &cfg, 13100U, true);

    power_monitor_output out = {0};
    for (int i = 0; i < 4; i++) {
        out = power_monitor_update(&state, &cfg, 11900U, false);
    }

    TEST_ASSERT_FALSE(out.warning_active);
    TEST_ASSERT_EQUAL_UINT16(11900U, out.instant_mv);
}

static void test_power_monitor_sustained_low_sets_warning(void)
{
    power_monitor_cfg cfg = power_monitor_default_cfg();
    cfg.warn_mv = 12000U;
    cfg.clear_mv = 12300U;
    cfg.warn_frames = 3U;
    cfg.filtered_shift = 0U;

    power_monitor_state state;
    power_monitor_init(&state);
    power_monitor_update(&state, &cfg, 13100U, true);

    power_monitor_output out = {0};
    for (int i = 0; i < 3; i++) {
        out = power_monitor_update(&state, &cfg, 11850U, false);
    }

    TEST_ASSERT_TRUE(out.warning_active);
}

static void test_power_monitor_warning_uses_clear_hysteresis(void)
{
    power_monitor_cfg cfg = power_monitor_default_cfg();
    cfg.warn_mv = 12000U;
    cfg.clear_mv = 12300U;
    cfg.warn_frames = 1U;
    cfg.filtered_shift = 0U;

    power_monitor_state state;
    power_monitor_init(&state);
    power_monitor_update(&state, &cfg, 11800U, false);
    TEST_ASSERT_TRUE(state.warning_active);

    power_monitor_update(&state, &cfg, 12100U, true);
    TEST_ASSERT_TRUE(state.warning_active);

    power_monitor_update(&state, &cfg, 12300U, true);
    TEST_ASSERT_FALSE(state.warning_active);
}

static void test_power_monitor_rest_estimate_updates_only_near_neutral(void)
{
    power_monitor_cfg cfg = power_monitor_default_cfg();
    cfg.rest_frames = 2U;
    cfg.filtered_shift = 0U;
    cfg.rest_shift = 0U;

    power_monitor_state state;
    power_monitor_init(&state);
    power_monitor_update(&state, &cfg, 13100U, true);

    power_monitor_update(&state, &cfg, 12300U, false);
    power_monitor_update(&state, &cfg, 12300U, false);
    TEST_ASSERT_EQUAL_UINT16(13100U, state.rest_estimate_mv);

    power_monitor_update(&state, &cfg, 13000U, true);
    power_monitor_update(&state, &cfg, 13000U, true);
    TEST_ASSERT_EQUAL_UINT16(13000U, state.rest_estimate_mv);
}

void run_power_monitor_tests(void)
{
    RUN_TEST(test_power_monitor_short_sag_does_not_warn);
    RUN_TEST(test_power_monitor_sustained_low_sets_warning);
    RUN_TEST(test_power_monitor_warning_uses_clear_hysteresis);
    RUN_TEST(test_power_monitor_rest_estimate_updates_only_near_neutral);
}
