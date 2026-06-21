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

static void test_arming_blocked_when_cruise_active(void)
{
    sm_inputs in = armable_inputs();
    in.cruise_active = true;

    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
    TEST_ASSERT_EQUAL(THROTTLE_TARGET_NEUTRAL, out.throttle_target);
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

/* --- Arm block reason (panel hint, same gate as can_arm) --- */

static void test_arm_reason_all_conditions_ready(void)
{
    sm_inputs in = armable_inputs();

    TEST_ASSERT_EQUAL(SM_ARM_READY, sm_arm_block_reason(&in));
}

static void test_arm_reason_no_rc(void)
{
    sm_inputs in = armable_inputs();
    in.rc_valid = false;

    TEST_ASSERT_EQUAL(SM_ARM_NO_RC, sm_arm_block_reason(&in));
}

static void test_arm_reason_throttle_not_neutral(void)
{
    sm_inputs in = armable_inputs();
    in.throttle_neutral = false;

    TEST_ASSERT_EQUAL(SM_ARM_THROTTLE_NOT_NEUTRAL, sm_arm_block_reason(&in));
}

static void test_arm_reason_cruise_active(void)
{
    sm_inputs in = armable_inputs();
    in.cruise_active = true;

    TEST_ASSERT_EQUAL(SM_ARM_CRUISE_ACTIVE, sm_arm_block_reason(&in));
}

static void test_arm_reason_calibrating(void)
{
    sm_inputs in = armable_inputs();
    in.calib_in_progress = true;

    TEST_ASSERT_EQUAL(SM_ARM_CALIBRATING, sm_arm_block_reason(&in));
}

static void test_arm_reason_settings_applying(void)
{
    sm_inputs in = armable_inputs();
    in.settings_apply_in_progress = true;

    TEST_ASSERT_EQUAL(SM_ARM_SETTINGS_APPLYING, sm_arm_block_reason(&in));
}

static void test_arm_reason_rc_dominates_throttle(void)
{
    /* Priority: with BOTH RC invalid AND throttle off-neutral, RC wins (the
     * same order as can_arm short-circuits). */
    sm_inputs in = armable_inputs();
    in.rc_valid = false;
    in.throttle_neutral = false;

    TEST_ASSERT_EQUAL(SM_ARM_NO_RC, sm_arm_block_reason(&in));
}

static void test_arm_reason_ready_regardless_of_intent(void)
{
    /* Intent-agnostic: READY even with no arm request (answers "could it arm?"). */
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false;

    TEST_ASSERT_EQUAL(SM_ARM_READY, sm_arm_block_reason(&in));
}

/* --- DEPLOY (manual motor raise) --- */

static void test_disarmed_deploy_request_enters_deploy(void)
{
    /* Arrange: a deploy request from DISARMED (no arm request). */
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false;
    in.deploy_request = true;

    /* Act */
    sm_outputs out = sm_step(SM_STATE_DISARMED, &in);

    /* Assert */
    TEST_ASSERT_EQUAL(SM_STATE_DEPLOY, out.state);
}

static void test_armed_deploy_request_does_not_enter_deploy(void)
{
    /* Oracle: DEPLOY is only reachable from DISARMED. An ARMED unit must NOT be
     * yanked into DEPLOY by a deploy request (it would drop the running drive). */
    sm_inputs in = armable_inputs();
    in.deploy_request = true;

    /* Act: ARMED with a deploy request but no disarm/RC loss. */
    sm_outputs out = sm_step(SM_STATE_ARMED, &in);

    /* Assert: stays ARMED, never DEPLOY. */
    TEST_ASSERT_EQUAL(SM_STATE_ARMED, out.state);
}

static void test_deploy_stow_request_exits_to_disarmed(void)
{
    /* Arrange: in DEPLOY with a stow request. */
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false;
    in.stow_request = true;

    /* Act */
    sm_outputs out = sm_step(SM_STATE_DEPLOY, &in);

    /* Assert: the only exit, to DISARMED. */
    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
}

static void test_deploy_rc_loss_without_stow_stays_deploy(void)
{
    /* Oracle: losing RC in DEPLOY must NOT drop to FAILSAFE; the motor is off, so
     * DEPLOY is already safe and the raised position is held. */
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false;
    in.rc_valid = false;     /* RC lost */
    in.stow_request = false; /* no exit requested */

    /* Act */
    sm_outputs out = sm_step(SM_STATE_DEPLOY, &in);

    /* Assert: stays in DEPLOY, NOT FAILSAFE. */
    TEST_ASSERT_EQUAL(SM_STATE_DEPLOY, out.state);
}

static void test_deploy_motor_off_and_servo_deploy(void)
{
    /* Oracle: throttle is ALWAYS neutral (motor off) in DEPLOY, and the servo
     * target is DEPLOY (hold deploy_servo_us), even with RC lost. */
    sm_inputs in = armable_inputs();
    in.ui_arm_request = false;
    in.rc_valid = false; /* prove servo stays DEPLOY regardless of RC */

    /* Act */
    sm_outputs out = sm_step(SM_STATE_DEPLOY, &in);

    /* Assert */
    TEST_ASSERT_EQUAL(SM_STATE_DEPLOY, out.state);
    TEST_ASSERT_EQUAL(THROTTLE_TARGET_NEUTRAL, out.throttle_target);
    TEST_ASSERT_EQUAL(SERVO_TARGET_DEPLOY, out.servo_target);
}

static void test_armed_instant_disarm_request(void)
{
    /* Oracle: a single disarm request from ARMED disarms immediately. */
    sm_inputs in = armable_inputs();
    in.ui_disarm_request = true;

    /* Act */
    sm_outputs out = sm_step(SM_STATE_ARMED, &in);

    /* Assert */
    TEST_ASSERT_EQUAL(SM_STATE_DISARMED, out.state);
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
    RUN_TEST(test_arming_blocked_when_cruise_active);
    RUN_TEST(test_armed_rc_invalid_goes_failsafe);
    RUN_TEST(test_armed_manual_disarm_goes_disarmed);
    RUN_TEST(test_armed_stays_armed_when_nominal);
    RUN_TEST(test_failsafe_persists_while_rc_invalid);
    RUN_TEST(test_failsafe_rc_recovery_goes_disarmed_not_armed);
    RUN_TEST(test_servo_tracks_when_rc_valid_disarmed);
    RUN_TEST(test_servo_tracks_when_rc_valid_armed);
    RUN_TEST(test_servo_centers_when_rc_invalid_regardless_of_state);
    RUN_TEST(test_calib_entry_all_conditions_enters);
    RUN_TEST(test_calib_entry_without_confirmation_refused);
    RUN_TEST(test_calib_entry_without_request_refused);
    RUN_TEST(test_calib_entry_without_neutral_refused);
    RUN_TEST(test_calib_entry_rc_invalid_goes_failsafe_not_calib);
    RUN_TEST(test_arm_reason_all_conditions_ready);
    RUN_TEST(test_arm_reason_no_rc);
    RUN_TEST(test_arm_reason_throttle_not_neutral);
    RUN_TEST(test_arm_reason_cruise_active);
    RUN_TEST(test_arm_reason_calibrating);
    RUN_TEST(test_arm_reason_settings_applying);
    RUN_TEST(test_arm_reason_rc_dominates_throttle);
    RUN_TEST(test_arm_reason_ready_regardless_of_intent);
    RUN_TEST(test_disarmed_deploy_request_enters_deploy);
    RUN_TEST(test_armed_deploy_request_does_not_enter_deploy);
    RUN_TEST(test_deploy_stow_request_exits_to_disarmed);
    RUN_TEST(test_deploy_rc_loss_without_stow_stays_deploy);
    RUN_TEST(test_deploy_motor_off_and_servo_deploy);
    RUN_TEST(test_armed_instant_disarm_request);
}
