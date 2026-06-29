#include "spot_lock.h"

#include "unity.h"

/* Reference / target point: ~52 N, 21 E. */
#define REF_LAT_E7 520000000
#define REF_LON_E7 210000000

/* Latitude offsets in degrees*1e7 for round metre distances at this latitude
 * (1 deg lat ~= 111195 m -> ~90 e7 units per metre). */
#define E7_1M 90     /* ~1 m   (inside a 3 m deadband) */
#define E7_10M 900   /* ~10 m  (outside the deadband) */
#define E7_100M 9000 /* ~100 m (drives throttle past the cap) */

/* Mild default tuning used across the regulator tests. */
#define DEADBAND_M 3
#define MAX_THROTTLE_NORM 350
#define THROTTLE_GAIN_PER_M 50
#define SERVO_GAIN_PER_DEG 20

static spot_lock_params make_params(void)
{
    spot_lock_params p = {
        .deadband_m = DEADBAND_M,
        .max_throttle_norm = MAX_THROTTLE_NORM,
        .throttle_gain_per_m = THROTTLE_GAIN_PER_M,
        .servo_gain_per_deg = SERVO_GAIN_PER_DEG,
    };
    return p;
}

/* A fully-permissive input: armed, CH3 on with a rising edge, sticks neutral,
 * fresh real fix, heading north, positioned exactly on the reference point. */
static spot_lock_inputs make_base_inputs(void)
{
    spot_lock_inputs in = {
        .armed = true,
        .ch3_on = true,
        .ch3_edge_on = true,
        .sticks_neutral = true,
        .gps_fresh = true,
        .gps_has_fix = true,
        .imu_ok = true,
        .lat_e7 = REF_LAT_E7,
        .lon_e7 = REF_LON_E7,
        .heading_deg10 = 0,
    };
    return in;
}

/* An ACTIVE carry-over state holding REF as the target. */
static spot_lock_state make_active_state(void)
{
    spot_lock_state st = {
        .substate = SPOT_LOCK_ACTIVE,
        .ref_lat_e7 = REF_LAT_E7,
        .ref_lon_e7 = REF_LON_E7,
    };
    return st;
}

void test_entry_on_edge_arms_active_and_snapshots_target(void)
{
    /* Arrange: OFF, all entry conditions satisfied, current pos != REF so the
     * snapshot is observably the CURRENT position. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.lat_e7 = REF_LAT_E7 + E7_10M;
    spot_lock_state st = { .substate = SPOT_LOCK_OFF };

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: ACTIVE, target snapshotted to the current position. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, out.substate);
    TEST_ASSERT_EQUAL_INT32(in.lat_e7, st.ref_lat_e7);
    TEST_ASSERT_EQUAL_INT32(in.lon_e7, st.ref_lon_e7);
}

void test_entry_blocked_when_disarmed(void)
{
    /* Arrange: every entry condition met EXCEPT armed. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.armed = false;
    spot_lock_state st = { .substate = SPOT_LOCK_OFF };

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: stays OFF (test fails if the armed gate is dropped). */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_OFF, out.substate);
}

void test_entry_blocked_without_real_fix(void)
{
    /* Arrange: fresh but NO real fix (the seed-fresh window must not arm). */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.gps_has_fix = false;
    spot_lock_state st = { .substate = SPOT_LOCK_OFF };

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: stays OFF (test fails if the real-fix gate is dropped). */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_OFF, out.substate);
}

void test_entry_blocked_when_stick_deflected(void)
{
    /* Arrange: every entry condition met EXCEPT sticks neutral. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.sticks_neutral = false;
    spot_lock_state st = { .substate = SPOT_LOCK_OFF };

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: stays OFF (test fails if the stick-neutral gate is dropped). */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_OFF, out.substate);
}

void test_entry_blocked_without_rising_edge(void)
{
    /* Arrange: CH3 held on (no edge) from OFF must not re-arm. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    spot_lock_state st = { .substate = SPOT_LOCK_OFF };

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: stays OFF (entry needs a fresh edge, not a held switch). */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_OFF, out.substate);
}

void test_deadband_inside_relaxes_actuators(void)
{
    /* Arrange: ACTIVE, current ~1 m from the target (inside the 3 m deadband),
     * with a deliberately misaligned heading so that WITHOUT the deadband the
     * servo would be non-zero (oracle power for the deadband). */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    in.lat_e7 = REF_LAT_E7 + E7_1M; /* current north of target */
    in.heading_deg10 = 0;           /* target lies south -> large bearing err */
    spot_lock_state st = make_active_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: relaxed - neutral throttle AND centered servo. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, out.substate);
    TEST_ASSERT_EQUAL_INT32(0, out.throttle_cmd);
    TEST_ASSERT_EQUAL_INT32(0, out.servo_cmd);
}

void test_deadband_outside_drives_throttle(void)
{
    /* Arrange: ACTIVE, current ~10 m SOUTH of the target with the bow pointing
     * north (bearing 0, heading 0 -> aligned, inside the gate). */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    in.lat_e7 = REF_LAT_E7 - E7_10M; /* current south -> target is north */
    in.heading_deg10 = 0;
    spot_lock_state st = make_active_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: positive forward thrust outside the deadband. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, out.substate);
    TEST_ASSERT_TRUE(out.throttle_cmd > 0);
}

void test_heading_gate_blocks_throttle_when_target_off_bow(void)
{
    /* Arrange: target is ~10 m to the north (bearing 0) but the bow points to
     * 280 deg -> bearing error +80 deg, outside the +/-60 gate. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    in.lat_e7 = REF_LAT_E7 - E7_10M;
    in.heading_deg10 = 2800; /* 280 deg -> error = 0 - 280 -> +80 deg */
    spot_lock_state st = make_active_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: no thrust (gate), but the servo steers toward the target. */
    TEST_ASSERT_EQUAL_INT32(0, out.throttle_cmd);
    TEST_ASSERT_TRUE(out.servo_cmd != 0);
}

void test_heading_gate_allows_throttle_when_aligned(void)
{
    /* Arrange: same geometry but bow at 350 deg -> error +10 deg, inside the
     * gate. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    in.lat_e7 = REF_LAT_E7 - E7_10M;
    in.heading_deg10 = 3500; /* 350 deg -> error +10 deg */
    spot_lock_state st = make_active_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: thrust applied when the bow is within the gate. */
    TEST_ASSERT_TRUE(out.throttle_cmd > 0);
}

void test_throttle_capped_at_max_for_large_distance(void)
{
    /* Arrange: ~100 m error with aligned bow. Raw P term (50 * 100 = 5000)
     * vastly exceeds the 350 cap, so removing the cap changes the result. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    in.lat_e7 = REF_LAT_E7 - E7_100M;
    in.heading_deg10 = 0;
    spot_lock_state st = make_active_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: exactly the cap, never above (R7). */
    TEST_ASSERT_EQUAL_INT32(MAX_THROTTLE_NORM, out.throttle_cmd);
}

void test_pause_on_gps_loss_then_resume_keeps_target(void)
{
    /* Arrange: ACTIVE loses a fresh fix. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    in.gps_fresh = false;
    spot_lock_state st = make_active_state();

    /* Act: GPS lost. */
    spot_lock_outputs paused = spot_lock_step(&in, &p, &st);

    /* Assert: PAUSED with relaxed actuators, target retained. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_PAUSED, paused.substate);
    TEST_ASSERT_EQUAL_INT32(0, paused.throttle_cmd);
    TEST_ASSERT_EQUAL_INT32(0, paused.servo_cmd);
    TEST_ASSERT_EQUAL_INT32(REF_LAT_E7, st.ref_lat_e7);

    /* Act: GPS returns. */
    in.gps_fresh = true;
    spot_lock_outputs resumed = spot_lock_step(&in, &p, &st);

    /* Assert: back to ACTIVE on the SAME target. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, resumed.substate);
    TEST_ASSERT_EQUAL_INT32(REF_LAT_E7, st.ref_lat_e7);
}

void test_pause_on_imu_loss(void)
{
    /* Arrange: ACTIVE loses fresh heading. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    in.imu_ok = false;
    spot_lock_state st = make_active_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: PAUSED, relaxed. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_PAUSED, out.substate);
    TEST_ASSERT_EQUAL_INT32(0, out.throttle_cmd);
    TEST_ASSERT_EQUAL_INT32(0, out.servo_cmd);
}

void test_abort_on_ch3_off(void)
{
    /* Arrange: ACTIVE, CH3 switched off. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    in.ch3_on = false;
    spot_lock_state st = make_active_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: immediate OFF. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_OFF, out.substate);
}

void test_abort_on_stick_out_of_deadband(void)
{
    /* Arrange: ACTIVE, a stick moved out of its neutral band (no extra
     * threshold - same neutral domain as the manual path, R4). */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    in.sticks_neutral = false;
    spot_lock_state st = make_active_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: immediate OFF. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_OFF, out.substate);
}

void test_step_is_deterministic(void)
{
    /* Arrange: identical inputs/state run twice must yield identical outputs. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    in.lat_e7 = REF_LAT_E7 - E7_10M;
    in.heading_deg10 = 0;
    spot_lock_state st_a = make_active_state();
    spot_lock_state st_b = make_active_state();

    /* Act */
    spot_lock_outputs a = spot_lock_step(&in, &p, &st_a);
    spot_lock_outputs b = spot_lock_step(&in, &p, &st_b);

    /* Assert: same decision both times. */
    TEST_ASSERT_EQUAL_INT(a.substate, b.substate);
    TEST_ASSERT_EQUAL_INT32(a.throttle_cmd, b.throttle_cmd);
    TEST_ASSERT_EQUAL_INT32(a.servo_cmd, b.servo_cmd);
    TEST_ASSERT_EQUAL_INT(a.bearing_deg10, b.bearing_deg10);
    TEST_ASSERT_EQUAL_INT(a.err_m, b.err_m);
}

void run_spot_lock_tests(void)
{
    RUN_TEST(test_entry_on_edge_arms_active_and_snapshots_target);
    RUN_TEST(test_entry_blocked_when_disarmed);
    RUN_TEST(test_entry_blocked_without_real_fix);
    RUN_TEST(test_entry_blocked_when_stick_deflected);
    RUN_TEST(test_entry_blocked_without_rising_edge);
    RUN_TEST(test_deadband_inside_relaxes_actuators);
    RUN_TEST(test_deadband_outside_drives_throttle);
    RUN_TEST(test_heading_gate_blocks_throttle_when_target_off_bow);
    RUN_TEST(test_heading_gate_allows_throttle_when_aligned);
    RUN_TEST(test_throttle_capped_at_max_for_large_distance);
    RUN_TEST(test_pause_on_gps_loss_then_resume_keeps_target);
    RUN_TEST(test_pause_on_imu_loss);
    RUN_TEST(test_abort_on_ch3_off);
    RUN_TEST(test_abort_on_stick_out_of_deadband);
    RUN_TEST(test_step_is_deterministic);
}
