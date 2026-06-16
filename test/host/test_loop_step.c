#include "loop_step.h"
#include "settings_validate.h"
#include "unity.h"

/* Capture-recency domain: 80 ticks/us. now_ticks and last_edge_ticks share it. */
#define TICKS_PER_US 80U
#define NOW_TICKS 1000000U

/* ESC neutral the throttle chain maps command 0 onto (default esc_neutral_us). */
#define ESC_NEUTRAL_US 1500U

static loop_validity_cfg make_cfg(void)
{
    rc_channel_cfg ch = {
        .width_min_us = 800U,
        .width_max_us = 2200U,
        .period_expected_us = 20000U,
        .period_tol_us = 8000U,
        .edge_timeout_us = 30000U,
    };
    loop_validity_cfg cfg = {.ch1 = ch, .ch2 = ch};
    return cfg;
}

/* A fresh, valid capture sample for a given pulse width. */
static rc_channel_sample sample_at(uint32_t width_us)
{
    rc_channel_sample s = {
        .width_us = width_us,
        .period_us = 20000U,
        .last_edge_ticks = NOW_TICKS - (1000U * TICKS_PER_US),
        .edge_seen = true,
    };
    return s;
}

/* An invalid sample: stale edge older than edge_timeout (40 ms ago). */
static rc_channel_sample stale_sample(uint32_t width_us)
{
    rc_channel_sample s = sample_at(width_us);
    s.last_edge_ticks = NOW_TICKS - (40000U * TICKS_PER_US);
    return s;
}

static loop_inputs make_inputs(uint32_t ch1_us, uint32_t ch2_us, bool arm)
{
    loop_inputs in = {
        .ch1 = sample_at(ch1_us),
        .ch2 = sample_at(ch2_us),
        .now_ticks = NOW_TICKS,
        .ui_arm_request = arm,
        .ui_disarm_request = false,
    };
    return in;
}

/* Drive the loop to ARMED: a few cycles of valid RC, neutral throttle, arm req.
 * The debounce starts latched-valid so one armable cycle is enough, but a couple
 * of cycles let any ramp/slew settle before the test perturbs the sticks. */
static void arm(loop_state *state, const loop_validity_cfg *cfg,
                const settings_params *params)
{
    loop_inputs in = make_inputs(1500U, 1500U, true);
    for (int i = 0; i < 3; i++) {
        loop_step(&in, cfg, params, state);
    }
}

static void test_disarmed_full_throttle_outputs_neutral_esc(void)
{
    /* Arrange: DISARMED (no arm request), throttle stick at max. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    loop_inputs in = make_inputs(1500U, 2000U, false);

    /* Act */
    loop_outputs out = loop_step(&in, &cfg, &params, &state);

    /* Assert: R7 gate keeps the ESC at neutral despite full throttle. */
    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.telemetry.state);
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
}

static void test_armed_throttle_tracks_with_ramp(void)
{
    /* Arrange: reach ARMED, then push throttle forward. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm(&state, &cfg, &params);
    loop_inputs in = make_inputs(1500U, 2000U, false);

    /* Act: one cycle of forward throttle while ARMED. */
    loop_outputs out = loop_step(&in, &cfg, &params, &state);

    /* Assert: armed and the ESC has ramped above neutral (toward forward). */
    TEST_ASSERT_EQUAL(SM_STATE_ARMED, out.telemetry.state);
    TEST_ASSERT_GREATER_THAN_UINT32(ESC_NEUTRAL_US, out.esc_us);
}

static void test_armed_servo_tracks_with_slew(void)
{
    /* Arrange: reach ARMED, then command steering toward one endpoint. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm(&state, &cfg, &params);

    uint32_t center = ((uint32_t)params.servo_min_us +
                       (uint32_t)params.servo_max_us) / 2U;
    loop_inputs in = make_inputs(2000U, 1500U, false);

    /* Act: drive the servo several cycles so the slew moves it off center. */
    loop_outputs out = {0};
    for (int i = 0; i < 5; i++) {
        out = loop_step(&in, &cfg, &params, &state);
    }

    /* Assert: servo has slewed away from center toward the commanded side. */
    TEST_ASSERT_EQUAL(SM_STATE_ARMED, out.telemetry.state);
    TEST_ASSERT_NOT_EQUAL(center, out.servo_us);
}

static void test_rc_invalid_failsafe_soft_stop_and_center(void)
{
    /* Arrange: ARMED at forward throttle and off-center steering, then RC dies
     * for enough consecutive frames to debounce-latch invalid. */
    settings_params params;
    settings_load_defaults(&params);
    loop_validity_cfg cfg = make_cfg();
    loop_state state;
    loop_state_init(&state, &params, RC_DEBOUNCE_DEFAULT_THRESHOLD);
    arm(&state, &cfg, &params);

    uint32_t center = ((uint32_t)params.servo_min_us +
                       (uint32_t)params.servo_max_us) / 2U;
    loop_inputs bad = {
        .ch1 = stale_sample(2000U),
        .ch2 = stale_sample(2000U),
        .now_ticks = NOW_TICKS,
        .ui_arm_request = false,
        .ui_disarm_request = false,
    };

    /* Act: run past the debounce threshold plus enough cycles for the ramp/slew
     * to reach neutral/center. */
    loop_outputs out = {0};
    for (int i = 0; i < 400; i++) {
        out = loop_step(&bad, &cfg, &params, &state);
    }

    /* Assert: latched FAILSAFE, ESC soft-stopped to neutral, servo centered. */
    TEST_ASSERT_EQUAL(SM_STATE_FAILSAFE, out.telemetry.state);
    TEST_ASSERT_FALSE(out.telemetry.rc_valid);
    TEST_ASSERT_EQUAL_UINT32(ESC_NEUTRAL_US, out.esc_us);
    TEST_ASSERT_EQUAL_UINT32(center, out.servo_us);
}

static void test_pending_applies_only_in_disarmed(void)
{
    /* The apply gate (R17/SI-6): pending params may be applied ONLY in DISARMED.
     * In every other state the gate refuses, so active params are unchanged. */
    TEST_ASSERT_TRUE(loop_should_apply_pending(SM_STATE_DISARMED));
    TEST_ASSERT_FALSE(loop_should_apply_pending(SM_STATE_ARMED));
    TEST_ASSERT_FALSE(loop_should_apply_pending(SM_STATE_FAILSAFE));
    TEST_ASSERT_FALSE(loop_should_apply_pending(SM_STATE_ESC_CALIBRATION));
}

void run_loop_step_tests(void)
{
    RUN_TEST(test_disarmed_full_throttle_outputs_neutral_esc);
    RUN_TEST(test_armed_throttle_tracks_with_ramp);
    RUN_TEST(test_armed_servo_tracks_with_slew);
    RUN_TEST(test_rc_invalid_failsafe_soft_stop_and_center);
    RUN_TEST(test_pending_applies_only_in_disarmed);
}
