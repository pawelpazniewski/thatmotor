#include "state_machine.h"
#include "unity.h"

/* A baseline "armable" input: RC valid, throttle neutral, explicit arm request,
 * no calibration / settings-apply in progress, no disarm request. Individual
 * tests flip one field to exercise a single transition. */
static sm_inputs armable_inputs(void)
{
    sm_inputs in = {
        .rc_valid = true,
        .throttle_neutral = true,
        .calib_in_progress = false,
        .settings_apply_in_progress = false,
        .ui_arm_request = true,
        .ui_disarm_request = false,
        .ui_calib_request = false,
        .ui_calib_confirm = false,
        .mode_toggle = false,
    };
    return in;
}

/* A baseline "calibration-entry" input: all entry conditions met (R15/SI-5).
 * Individual tests drop one field to prove entry is refused without it. */
static sm_inputs calib_entry_inputs(void)
{
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false;   /* calibration request, not an arm request */
    in.ui_calib_request = true;
    in.ui_calib_confirm = true;
    return in;
}

/* --- Boot --- */

static void test_boot_disarmed_regardless_of_inputs(void)
{
    /* Arrange: callers always start sm at DISARMED; here we confirm the very
     * first transition out of DISARMED with no arm request stays DISARMED,
     * mirroring "boot lands in DISARMED" independent of any reset reason. */
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false;

    /* Act */
    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    /* Assert */
    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
}

/* --- DISARMED --- */

static void test_disarmed_all_conditions_arms(void)
{
    sm_inputs in = armable_inputs();

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_ARMED, out.state);
    TEST_ASSERT_EQUAL(THROTTLE_TARGET_TRACK, out.throttle_target);
}

static void test_disarmed_without_neutral_stays_disarmed(void)
{
    sm_inputs in = armable_inputs();
    in.throttle_neutral = false;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
    TEST_ASSERT_EQUAL(THROTTLE_TARGET_NEUTRAL, out.throttle_target);
}

static void test_disarmed_without_arm_request_stays_disarmed(void)
{
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
}

static void test_disarmed_rc_invalid_goes_failsafe(void)
{
    sm_inputs in = armable_inputs();
    in.rc_valid = false;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_FAILSAFE, out.state);
}

static void test_arming_blocked_when_calib_in_progress(void)
{
    sm_inputs in = armable_inputs();
    in.calib_in_progress = true;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
}

static void test_arming_blocked_when_settings_apply_in_progress(void)
{
    sm_inputs in = armable_inputs();
    in.settings_apply_in_progress = true;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
}

/* --- ARMED --- */

static void test_armed_rc_invalid_goes_failsafe(void)
{
    sm_inputs in = armable_inputs();
    in.rc_valid = false;

    sm_outputs out = sm_step(SM_STATE_ARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_FAILSAFE, out.state);
    TEST_ASSERT_EQUAL(THROTTLE_TARGET_NEUTRAL, out.throttle_target);
}

static void test_armed_manual_disarm_goes_disarmed(void)
{
    sm_inputs in = armable_inputs();
    in.ui_disarm_request = true;

    sm_outputs out = sm_step(SM_STATE_ARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
}

static void test_armed_stays_armed_when_nominal(void)
{
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false; /* already armed, no re-request needed */

    sm_outputs out = sm_step(SM_STATE_ARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_ARMED, out.state);
    TEST_ASSERT_EQUAL(THROTTLE_TARGET_TRACK, out.throttle_target);
}

/* --- FAILSAFE (latched) --- */

static void test_failsafe_persists_while_rc_invalid(void)
{
    sm_inputs in = armable_inputs();
    in.rc_valid = false;

    sm_outputs out = sm_step(SM_STATE_FAILSAFE, &in);

    TEST_ASSERT_EQUAL(SM_STATE_FAILSAFE, out.state);
    TEST_ASSERT_EQUAL(THROTTLE_TARGET_NEUTRAL, out.throttle_target);
}

static void test_failsafe_rc_recovery_goes_disarmed_not_armed(void)
{
    /* Even with a fully armable input set, RC recovery must land in DISARMED,
     * never straight back to ARMED. */
    sm_inputs in = armable_inputs();

    sm_outputs out = sm_step(SM_STATE_FAILSAFE, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
}

/* --- Servo rule independent of arming --- */

static void test_servo_tracks_when_rc_valid_disarmed(void)
{
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false; /* stay DISARMED */

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
    TEST_ASSERT_EQUAL(SERVO_TARGET_TRACK, out.servo_target);
}

static void test_servo_tracks_when_rc_valid_armed(void)
{
    sm_inputs in = armable_inputs();

    sm_outputs out = sm_step(SM_STATE_ARMED, &in);

    TEST_ASSERT_EQUAL(SERVO_TARGET_TRACK, out.servo_target);
}

static void test_servo_centers_when_rc_invalid_regardless_of_state(void)
{
    sm_inputs in = armable_inputs();
    in.rc_valid = false;

    sm_outputs from_armed = sm_step(SM_STATE_ARMED, &in);
    sm_outputs from_disarmed = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SERVO_TARGET_CENTER, from_armed.servo_target);
    TEST_ASSERT_EQUAL(SERVO_TARGET_CENTER, from_disarmed.servo_target);
}

/* --- CH4 mode toggle (ARMED <-> DISARMED via the same guard) --- */

static void test_disarmed_mode_toggle_with_guard_met_arms(void)
{
    /* Arrange: no panel arm request; the CH4 toggle is the arm intent, with the
     * full safety gate satisfied (RC valid, throttle neutral). */
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false;
    in.mode_toggle = true;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_ARMED, out.state);
    TEST_ASSERT_EQUAL(THROTTLE_TARGET_TRACK, out.throttle_target);
}

static void test_disarmed_mode_toggle_without_neutral_stays_disarmed(void)
{
    /* The toggle passes the SAME guard as a panel arm: off-neutral throttle must
     * block arming, so a toggle at the throttle does NOT spin the motor. */
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false;
    in.mode_toggle = true;
    in.throttle_neutral = false;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
    TEST_ASSERT_EQUAL(THROTTLE_TARGET_NEUTRAL, out.throttle_target);
}

static void test_armed_mode_toggle_disarms(void)
{
    /* Arrange: ARMED with no panel disarm; the CH4 toggle alone disarms. */
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false;
    in.ui_disarm_request = false;
    in.mode_toggle = true;

    sm_outputs out = sm_step(SM_STATE_ARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
}

static void test_failsafe_mode_toggle_stays_failsafe_without_rc(void)
{
    /* RC dominates: a toggle must never lift FAILSAFE while RC is invalid. */
    sm_inputs in = armable_inputs();
    in.rc_valid = false;
    in.mode_toggle = true;

    sm_outputs out = sm_step(SM_STATE_FAILSAFE, &in);

    TEST_ASSERT_EQUAL(SM_STATE_FAILSAFE, out.state);
}

/* --- ESC calibration entry guard (R15/SI-5) --- */

static void test_calib_entry_all_conditions_enters(void)
{
    sm_inputs in = calib_entry_inputs();

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_ESC_CALIBRATION, out.state);
}

static void test_calib_entry_without_confirmation_refused(void)
{
    /* SI-5: never start without the confirmed removal warning. */
    sm_inputs in = calib_entry_inputs();
    in.ui_calib_confirm = false;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
}

static void test_calib_entry_without_request_refused(void)
{
    /* SI-5: never start automatically (no explicit UI request). */
    sm_inputs in = calib_entry_inputs();
    in.ui_calib_request = false;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
}

static void test_calib_entry_without_neutral_refused(void)
{
    sm_inputs in = calib_entry_inputs();
    in.throttle_neutral = false;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
}

static void test_calib_entry_rc_invalid_goes_failsafe_not_calib(void)
{
    sm_inputs in = calib_entry_inputs();
    in.rc_valid = false;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_FAILSAFE, out.state);
}

void run_state_machine_tests(void)
{
    RUN_TEST(test_boot_disarmed_regardless_of_inputs);
    RUN_TEST(test_disarmed_all_conditions_arms);
    RUN_TEST(test_disarmed_without_neutral_stays_disarmed);
    RUN_TEST(test_disarmed_without_arm_request_stays_disarmed);
    RUN_TEST(test_disarmed_rc_invalid_goes_failsafe);
    RUN_TEST(test_arming_blocked_when_calib_in_progress);
    RUN_TEST(test_arming_blocked_when_settings_apply_in_progress);
    RUN_TEST(test_armed_rc_invalid_goes_failsafe);
    RUN_TEST(test_armed_manual_disarm_goes_disarmed);
    RUN_TEST(test_armed_stays_armed_when_nominal);
    RUN_TEST(test_failsafe_persists_while_rc_invalid);
    RUN_TEST(test_failsafe_rc_recovery_goes_disarmed_not_armed);
    RUN_TEST(test_disarmed_mode_toggle_with_guard_met_arms);
    RUN_TEST(test_disarmed_mode_toggle_without_neutral_stays_disarmed);
    RUN_TEST(test_armed_mode_toggle_disarms);
    RUN_TEST(test_failsafe_mode_toggle_stays_failsafe_without_rc);
    RUN_TEST(test_servo_tracks_when_rc_valid_disarmed);
    RUN_TEST(test_servo_tracks_when_rc_valid_armed);
    RUN_TEST(test_servo_centers_when_rc_invalid_regardless_of_state);
    RUN_TEST(test_calib_entry_all_conditions_enters);
    RUN_TEST(test_calib_entry_without_confirmation_refused);
    RUN_TEST(test_calib_entry_without_request_refused);
    RUN_TEST(test_calib_entry_without_neutral_refused);
    RUN_TEST(test_calib_entry_rc_invalid_goes_failsafe_not_calib);
}
