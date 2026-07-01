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

/* Goto cruise-decel profile tuning: cruise at 600 (normalized) beyond 20 m, then
 * ramp linearly down to the deadband edge. Chosen so the mid-zone value differs
 * from BOTH cruise (removed ramp) and the SRC_HOLD gain x dist profile. */
#define GOTO_SLOWDOWN_M 20
#define GOTO_CRUISE_NORM 600

static spot_lock_params make_params(void)
{
    spot_lock_params p = {
        .deadband_m = DEADBAND_M,
        .max_throttle_norm = MAX_THROTTLE_NORM,
        .throttle_gain_per_m = THROTTLE_GAIN_PER_M,
        .servo_gain_per_deg = SERVO_GAIN_PER_DEG,
        .goto_slowdown_distance_m = GOTO_SLOWDOWN_M,
        .goto_cruise_norm = GOTO_CRUISE_NORM,
    };
    return p;
}

/* Metre offsets in latitude e7 for the goto-profile tests (~90 e7 per metre). */
#define E7_5M 450   /* ~5 m  (just above the 3 m deadband, deep in the ramp) */

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

/* An ACTIVE CH3-hold carry-over state holding REF as the target. */
static spot_lock_state make_active_state(void)
{
    spot_lock_state st = {
        .substate = SPOT_LOCK_ACTIVE,
        .target_source = SPOT_LOCK_SRC_HOLD,
        .ref_lat_e7 = REF_LAT_E7,
        .ref_lon_e7 = REF_LON_E7,
    };
    return st;
}

/* A goto target offset ~10 m east of REF (distinct from any current position
 * placed to the north), so a snapshot is observably NOT the goto point. */
#define GOTO_LAT_E7 REF_LAT_E7
#define GOTO_LON_E7 (REF_LON_E7 + E7_10M)

/* Base inputs for an app-driven goto: ARMED, sticks neutral, CH3 OFF, fresh
 * GPS/IMU and fresh app link, goto latched to the external target. */
static spot_lock_inputs make_goto_inputs(void)
{
    spot_lock_inputs in = make_base_inputs();
    in.ch3_on = false;
    in.ch3_edge_on = false;
    in.goto_engage = true;
    in.goto_lat_e7 = GOTO_LAT_E7;
    in.goto_lon_e7 = GOTO_LON_E7;
    in.comms_fresh = true;
    return in;
}

/* An ACTIVE goto carry-over state referencing the external goto target. */
static spot_lock_state make_active_goto_state(void)
{
    spot_lock_state st = {
        .substate = SPOT_LOCK_ACTIVE,
        .target_source = SPOT_LOCK_SRC_GOTO,
        .ref_lat_e7 = GOTO_LAT_E7,
        .ref_lon_e7 = GOTO_LON_E7,
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

void test_pause_on_fix_loss_then_resume_keeps_target(void)
{
    /* Arrange: ACTIVE, current ~10 m from the target so a held output would be
     * observably non-neutral, but the fix is lost while still fresh (seed-fresh
     * window). Re-validating the fix during hold must pause - if dropped, step 4
     * would run and yield ACTIVE with thrust (oracle power). */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs();
    in.ch3_edge_on = false;
    in.lat_e7 = REF_LAT_E7 - E7_10M; /* target north, bow aligned -> would thrust */
    in.heading_deg10 = 0;
    in.gps_fresh = true;  /* still inside the freshness window */
    in.gps_has_fix = false; /* but no real fix */
    in.imu_ok = true;
    spot_lock_state st = make_active_state();

    /* Act: fix lost. */
    spot_lock_outputs paused = spot_lock_step(&in, &p, &st);

    /* Assert: PAUSED with relaxed actuators, target retained. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_PAUSED, paused.substate);
    TEST_ASSERT_EQUAL_INT32(0, paused.throttle_cmd);
    TEST_ASSERT_EQUAL_INT32(0, paused.servo_cmd);
    TEST_ASSERT_EQUAL_INT32(REF_LAT_E7, st.ref_lat_e7);

    /* Act: fix returns. */
    in.gps_has_fix = true;
    spot_lock_outputs resumed = spot_lock_step(&in, &p, &st);

    /* Assert: back to ACTIVE on the SAME target. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, resumed.substate);
    TEST_ASSERT_EQUAL_INT32(REF_LAT_E7, st.ref_lat_e7);
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

/* --- Unit 3: goto source arbitration + comms gate (R2-R6) --- */

void test_goto_engages_active_with_external_target(void)
{
    /* Arrange: OFF, no CH3, goto latched with fresh sensors + link. Current is
     * ~10 m south of the external target with the bow aligned north, so a
     * running goto must drive forward thrust. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_goto_inputs();
    in.lat_e7 = REF_LAT_E7 - E7_10M; /* target (REF) lies north, bow aligned */
    in.heading_deg10 = 0;
    spot_lock_state st = { .substate = SPOT_LOCK_OFF,
                           .target_source = SPOT_LOCK_SRC_NONE };

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: ACTIVE on the EXTERNAL goto target (not a snapshot), driving. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, out.substate);
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_SRC_GOTO, st.target_source);
    TEST_ASSERT_EQUAL_INT32(GOTO_LAT_E7, st.ref_lat_e7);
    TEST_ASSERT_EQUAL_INT32(GOTO_LON_E7, st.ref_lon_e7);
    TEST_ASSERT_TRUE(out.throttle_cmd > 0);
}

void test_ch3_preempts_active_goto_and_snapshots_here_and_now(void)
{
    /* Arrange: a goto is ACTIVE on the external target; CH3 comes on (edge) with
     * the current position ~10 m NORTH of the goto point. CH3 must preempt goto
     * and snapshot the CURRENT position - not keep the goto target. Removing the
     * preemption (entry keyed only on OFF) leaves ref at the goto point -> fail. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs(); /* CH3 on, rising edge, fresh */
    in.lat_e7 = REF_LAT_E7 + E7_10M;          /* current != goto target */
    in.goto_engage = true;
    in.goto_lat_e7 = GOTO_LAT_E7;
    in.goto_lon_e7 = GOTO_LON_E7;
    spot_lock_state st = make_active_goto_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: SRC_HOLD on the current position snapshot, not the goto target. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_SRC_HOLD, st.target_source);
    TEST_ASSERT_EQUAL_INT32(in.lat_e7, st.ref_lat_e7);
    TEST_ASSERT_EQUAL_INT32(in.lon_e7, st.ref_lon_e7);
    TEST_ASSERT_TRUE(st.ref_lat_e7 != GOTO_LAT_E7);
    TEST_ASSERT_NOT_EQUAL(SPOT_LOCK_OFF, out.substate);
}

void test_goto_pauses_on_comms_loss_then_resumes_same_target(void)
{
    /* Arrange: goto ACTIVE ~10 m from the target, app link goes stale. The comms
     * gate applies to SRC_GOTO: link loss must pause (neutral+center), target
     * retained. Removing the comms gate keeps it ACTIVE with thrust -> fail. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_goto_inputs();
    in.lat_e7 = REF_LAT_E7 - E7_10M;
    in.comms_fresh = false;
    spot_lock_state st = make_active_goto_state();

    /* Act: link lost. */
    spot_lock_outputs paused = spot_lock_step(&in, &p, &st);

    /* Assert: PAUSED, relaxed, goto target retained. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_PAUSED, paused.substate);
    TEST_ASSERT_EQUAL_INT32(0, paused.throttle_cmd);
    TEST_ASSERT_EQUAL_INT32(0, paused.servo_cmd);
    TEST_ASSERT_EQUAL_INT32(GOTO_LAT_E7, st.ref_lat_e7);
    TEST_ASSERT_EQUAL_INT32(GOTO_LON_E7, st.ref_lon_e7);

    /* Act: link returns. */
    in.comms_fresh = true;
    spot_lock_outputs resumed = spot_lock_step(&in, &p, &st);

    /* Assert: back to ACTIVE on the SAME external target. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, resumed.substate);
    TEST_ASSERT_EQUAL_INT32(GOTO_LON_E7, st.ref_lon_e7);
}

void test_goto_pauses_on_gps_loss_keeps_target(void)
{
    /* Arrange: goto ACTIVE ~10 m from the external target with the bow aligned
     * (would thrust), then the GPS fix is lost while the app link stays fresh.
     * SRC_GOTO must pause on sensor loss too (not just link loss), symmetric to
     * SRC_HOLD, and RETAIN the external goto target. Oracle: dropping the
     * sensor-loss pause for goto keeps it ACTIVE with thrust -> this fails. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_goto_inputs();
    in.lat_e7 = REF_LAT_E7 - E7_10M;
    in.heading_deg10 = 0; /* aligned toward the target */
    in.gps_has_fix = false; /* fix lost (still fresh window: seed-fresh) */
    in.comms_fresh = true;  /* link is fine; the loss is the sensor, not comms */
    spot_lock_state st = make_active_goto_state();

    /* Act: fix lost. */
    spot_lock_outputs paused = spot_lock_step(&in, &p, &st);

    /* Assert: PAUSED, relaxed, external goto target retained. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_PAUSED, paused.substate);
    TEST_ASSERT_EQUAL_INT32(0, paused.throttle_cmd);
    TEST_ASSERT_EQUAL_INT32(0, paused.servo_cmd);
    TEST_ASSERT_EQUAL_INT32(GOTO_LAT_E7, st.ref_lat_e7);
    TEST_ASSERT_EQUAL_INT32(GOTO_LON_E7, st.ref_lon_e7);

    /* Act: fix returns. */
    in.gps_has_fix = true;
    spot_lock_outputs resumed = spot_lock_step(&in, &p, &st);

    /* Assert: back to ACTIVE on the SAME external target. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, resumed.substate);
    TEST_ASSERT_EQUAL_INT32(GOTO_LAT_E7, st.ref_lat_e7);
    TEST_ASSERT_EQUAL_INT32(GOTO_LON_E7, st.ref_lon_e7);
}

void test_comms_gate_does_not_pause_ch3_hold(void)
{
    /* Arrange: SRC_HOLD ACTIVE ~10 m from target, bow aligned, but the app link
     * is stale. Link freshness must NOT gate the RC-owned CH3 hold - it keeps
     * driving. Extending the comms gate to SRC_HOLD would pause here -> fail. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_base_inputs(); /* CH3 on, fresh GPS/IMU */
    in.ch3_edge_on = false;
    in.lat_e7 = REF_LAT_E7 - E7_10M;
    in.heading_deg10 = 0;
    in.comms_fresh = false; /* link down - must be ignored for SRC_HOLD */
    spot_lock_state st = make_active_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: still ACTIVE and driving despite the stale link. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, out.substate);
    TEST_ASSERT_TRUE(out.throttle_cmd > 0);
}

void test_goto_override_on_stick_deflection(void)
{
    /* Arrange: goto ACTIVE, a stick leaves its neutral band. Manual override
     * wins structurally -> OFF and source cleared. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_goto_inputs();
    in.sticks_neutral = false;
    spot_lock_state st = make_active_goto_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: OFF, no engaged source. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_OFF, out.substate);
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_SRC_NONE, st.target_source);
}

void test_goto_retains_target_during_pause_ignoring_input(void)
{
    /* Arrange: goto ACTIVE on the external target, then the link goes stale AND
     * the loop presents a ZEROED goto_* (the null-island hazard: upstream drops
     * the latch on link loss). The pure core must PAUSE and RETAIN the last good
     * ref_* - never overwrite it from the input while comms is stale. A naive
     * "ref_* = goto_* every cycle" writes (0,0) here -> this test fails (oracle). */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_goto_inputs();
    in.lat_e7 = REF_LAT_E7 - E7_10M;
    in.comms_fresh = false;
    in.goto_lat_e7 = 0; /* upstream zeroed the target on link loss */
    in.goto_lon_e7 = 0;
    spot_lock_state st = make_active_goto_state();

    /* Act: link lost with a zeroed input target. */
    spot_lock_outputs paused = spot_lock_step(&in, &p, &st);

    /* Assert: PAUSED and the ORIGINAL target retained (not 0,0, not the input). */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_PAUSED, paused.substate);
    TEST_ASSERT_EQUAL_INT32(GOTO_LAT_E7, st.ref_lat_e7);
    TEST_ASSERT_EQUAL_INT32(GOTO_LON_E7, st.ref_lon_e7);

    /* Act: link returns and the upstream restores the latched target. */
    in.comms_fresh = true;
    in.goto_lat_e7 = GOTO_LAT_E7;
    in.goto_lon_e7 = GOTO_LON_E7;
    spot_lock_outputs resumed = spot_lock_step(&in, &p, &st);

    /* Assert: back to ACTIVE on the SAME retained target. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, resumed.substate);
    TEST_ASSERT_EQUAL_INT32(GOTO_LAT_E7, st.ref_lat_e7);
    TEST_ASSERT_EQUAL_INT32(GOTO_LON_E7, st.ref_lon_e7);
}

void test_goto_fresh_link_tracks_new_target(void)
{
    /* Arrange: goto ACTIVE on the current target; while the link is FRESH a new
     * goto point arrives. R1: a fresh link must re-latch ref_* to the new target
     * (live tracking is intended when the link is up). Retaining ONLY on entry
     * would leave ref at the old point -> this test fails (oracle for R1). */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_goto_inputs();
    in.comms_fresh = true;
    in.goto_lat_e7 = GOTO_LAT_E7 + E7_10M; /* new, distinct target */
    in.goto_lon_e7 = GOTO_LON_E7 + E7_10M;
    spot_lock_state st = make_active_goto_state(); /* ref = old GOTO point */

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: ref re-latched to the NEW target (fresh link replaces the goto). */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_SRC_GOTO, st.target_source);
    TEST_ASSERT_EQUAL_INT32(GOTO_LAT_E7 + E7_10M, st.ref_lat_e7);
    TEST_ASSERT_EQUAL_INT32(GOTO_LON_E7 + E7_10M, st.ref_lon_e7);
    TEST_ASSERT_NOT_EQUAL(SPOT_LOCK_OFF, out.substate);
}

void test_goto_arrived_flag_tracks_deadband(void)
{
    /* Arrange: goto ACTIVE exactly on target -> arrived; then ~10 m off -> not
     * arrived. Oracle: a hard-coded arrived fails one branch or the other. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_goto_inputs();
    in.lat_e7 = GOTO_LAT_E7; /* on target */
    in.lon_e7 = GOTO_LON_E7;
    spot_lock_state on_st = make_active_goto_state();

    /* Act: on target. */
    spot_lock_outputs on_target = spot_lock_step(&in, &p, &on_st);

    /* Assert: arrived and relaxed - inside the deadband throttle is neutral. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, on_target.substate);
    TEST_ASSERT_TRUE(on_target.arrived);
    TEST_ASSERT_EQUAL_INT32(0, on_target.throttle_cmd);

    /* Act: ~10 m off target. */
    in.lon_e7 = GOTO_LON_E7 + E7_10M;
    spot_lock_state off_st = make_active_goto_state();
    spot_lock_outputs off_target = spot_lock_step(&in, &p, &off_st);

    /* Assert: not arrived. */
    TEST_ASSERT_FALSE(off_target.arrived);
}

/* --- Goto cruise-decel throttle profile (SRC_GOTO) --- */

/* An ACTIVE goto whose target is REF (not the east-offset GOTO point), so the
 * position offsets used below map cleanly onto the ramp distance. The fresh link
 * re-latches ref_* to goto_lat/lon each cycle, so goto_* is set to REF too. */
static spot_lock_inputs make_goto_to_ref_inputs(void)
{
    spot_lock_inputs in = make_goto_inputs();
    in.goto_lat_e7 = REF_LAT_E7;
    in.goto_lon_e7 = REF_LON_E7;
    in.heading_deg10 = 0; /* bow north, target north -> inside the gate */
    return in;
}

static spot_lock_state make_goto_to_ref_state(void)
{
    spot_lock_state st = {
        .substate = SPOT_LOCK_ACTIVE,
        .target_source = SPOT_LOCK_SRC_GOTO,
        .ref_lat_e7 = REF_LAT_E7,
        .ref_lon_e7 = REF_LON_E7,
    };
    return st;
}

void test_goto_cruises_at_full_beyond_slowdown(void)
{
    /* Arrange: goto ACTIVE ~100 m from the target (>> the 20 m slowdown), bow
     * aligned. Beyond the slowdown distance the profile holds full cruise. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_goto_to_ref_inputs();
    in.lat_e7 = REF_LAT_E7 - E7_100M; /* target north, bow aligned */
    spot_lock_state st = make_goto_to_ref_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: exactly the cruise ceiling (removing the >= slowdown branch, i.e.
     * ramping here, would yield less than cruise -> oracle). */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, out.substate);
    TEST_ASSERT_EQUAL_INT32(GOTO_CRUISE_NORM, out.throttle_cmd);
}

void test_goto_ramps_proportionally_in_slowdown_zone(void)
{
    /* Arrange: goto ACTIVE ~10 m from the target: inside the 20 m slowdown zone,
     * well outside the 3 m deadband. The thrust must scale with distance. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_goto_to_ref_inputs();
    in.lat_e7 = REF_LAT_E7 - E7_10M; /* ~10 m south, target north */
    spot_lock_state st = make_goto_to_ref_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: proportional to (dist - deadband) / (slowdown - deadband). Computed
     * from the reported err_m (integer metres) within a rounding margin. This
     * FAILS if the ramp is removed (would be cruise 600) AND if the SRC_HOLD
     * profile were used here (would be gain*dist capped = 350) -> oracle. */
    int32_t span = GOTO_SLOWDOWN_M - DEADBAND_M;
    int32_t expected =
        (int32_t)GOTO_CRUISE_NORM * ((int32_t)out.err_m - DEADBAND_M) / span;
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, out.substate);
    TEST_ASSERT_TRUE(out.throttle_cmd > 0);
    TEST_ASSERT_TRUE(out.throttle_cmd < GOTO_CRUISE_NORM);
    TEST_ASSERT_INT_WITHIN(30, expected, out.throttle_cmd);
}

void test_goto_throttle_small_just_above_deadband(void)
{
    /* Arrange: goto ACTIVE ~5 m from the target: just above the 3 m deadband,
     * near the bottom of the ramp. Thrust must be small (a gentle final crawl). */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_goto_to_ref_inputs();
    in.lat_e7 = REF_LAT_E7 - E7_5M; /* ~5 m south, target north */
    spot_lock_state st = make_goto_to_ref_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: positive but far below cruise (removed ramp -> cruise 600). */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, out.substate);
    TEST_ASSERT_TRUE(out.throttle_cmd > 0);
    TEST_ASSERT_TRUE(out.throttle_cmd < GOTO_CRUISE_NORM / 4);
}

void test_goto_relaxes_inside_deadband(void)
{
    /* Arrange: goto ACTIVE ~1 m from the target (inside the 3 m deadband). The
     * shared deadband early-return relaxes the throttle to neutral. */
    spot_lock_params p = make_params();
    spot_lock_inputs in = make_goto_to_ref_inputs();
    in.lat_e7 = REF_LAT_E7 + E7_1M; /* ~1 m from target, inside deadband */
    spot_lock_state st = make_goto_to_ref_state();

    /* Act */
    spot_lock_outputs out = spot_lock_step(&in, &p, &st);

    /* Assert: relaxed - neutral throttle, arrived flagged. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, out.substate);
    TEST_ASSERT_EQUAL_INT32(0, out.throttle_cmd);
    TEST_ASSERT_TRUE(out.arrived);
}

void test_goto_and_hold_differ_at_same_distance(void)
{
    /* Arrange: identical geometry (~10 m south of REF, bow aligned) and identical
     * params, run once as SRC_GOTO and once as SRC_HOLD. The two sources MUST
     * pick different thrust profiles: cruise-decel (goto) vs gain x dist capped
     * (hold). If both used one function the values would match -> this fails. */
    spot_lock_params p = make_params();

    spot_lock_inputs goto_in = make_goto_to_ref_inputs();
    goto_in.lat_e7 = REF_LAT_E7 - E7_10M;
    spot_lock_state goto_st = make_goto_to_ref_state();
    spot_lock_outputs goto_out = spot_lock_step(&goto_in, &p, &goto_st);

    spot_lock_inputs hold_in = make_base_inputs(); /* CH3 on -> SRC_HOLD */
    hold_in.ch3_edge_on = false;
    hold_in.lat_e7 = REF_LAT_E7 - E7_10M;
    hold_in.heading_deg10 = 0;
    spot_lock_state hold_st = make_active_state(); /* SRC_HOLD, ref = REF */
    spot_lock_outputs hold_out = spot_lock_step(&hold_in, &p, &hold_st);

    /* Assert: both drive, but with distinct profiles at the same distance. */
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_SRC_GOTO, goto_st.target_source);
    TEST_ASSERT_EQUAL_INT(SPOT_LOCK_SRC_HOLD, hold_st.target_source);
    TEST_ASSERT_TRUE(goto_out.throttle_cmd > 0);
    TEST_ASSERT_TRUE(hold_out.throttle_cmd > 0);
    TEST_ASSERT_TRUE(goto_out.throttle_cmd != hold_out.throttle_cmd);
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
    RUN_TEST(test_pause_on_fix_loss_then_resume_keeps_target);
    RUN_TEST(test_abort_on_ch3_off);
    RUN_TEST(test_abort_on_stick_out_of_deadband);
    RUN_TEST(test_step_is_deterministic);
    RUN_TEST(test_goto_engages_active_with_external_target);
    RUN_TEST(test_ch3_preempts_active_goto_and_snapshots_here_and_now);
    RUN_TEST(test_goto_pauses_on_comms_loss_then_resumes_same_target);
    RUN_TEST(test_goto_pauses_on_gps_loss_keeps_target);
    RUN_TEST(test_comms_gate_does_not_pause_ch3_hold);
    RUN_TEST(test_goto_override_on_stick_deflection);
    RUN_TEST(test_goto_retains_target_during_pause_ignoring_input);
    RUN_TEST(test_goto_fresh_link_tracks_new_target);
    RUN_TEST(test_goto_arrived_flag_tracks_deadband);
    RUN_TEST(test_goto_cruises_at_full_beyond_slowdown);
    RUN_TEST(test_goto_ramps_proportionally_in_slowdown_zone);
    RUN_TEST(test_goto_throttle_small_just_above_deadband);
    RUN_TEST(test_goto_relaxes_inside_deadband);
    RUN_TEST(test_goto_and_hold_differ_at_same_distance);
}
