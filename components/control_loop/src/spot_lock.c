#include "spot_lock.h"

#include <math.h>
#include <stdint.h>

#include "geo_math.h"

/* Omnidirectional drive (replaces the old +/-60 deg forward-thrust gate). We
 * ALWAYS thrust toward the target and let the bow-mounted motor both pull and
 * rotate the hull, so there is no zero-thrust dead zone and the boat can never
 * stall unable to turn (the deadlock the +/-60 gate caused). Two shaping rules:
 *
 * Toward/away split: if the target sits more than +/-90 deg off the bow it is
 * "behind", so it is shorter to REVERSE toward it than to swing the whole hull
 * around. Past this split we drive reverse and measure the steering error to the
 * reversed bow, so the servo never needs more than +/-90 deg of authority. */
#define SPOT_LOCK_REVERSE_SPLIT_DEG10 900

/* Alignment throttle taper: thrust scales with cos(steering error) so the boat
 * eases power while still swinging into line and builds to full as it aligns
 * (adaptive turn). A floor keeps a minimum thrust even at the +/-90 deg edge so
 * turning authority -- and thus escape from any misalignment -- is never lost. */
#define SPOT_LOCK_ALIGN_FLOOR 0.2

/* Radians per (degree * 10): 0.1 deg * pi/180. For the cos() alignment taper. */
#define SPOT_LOCK_DEG10_TO_RAD (3.14159265358979323846 / 1800.0)

/* GOTO drive is FORWARD-ONLY and "turn the bow first, then go" -- reverse is
 * unsafe while travelling (you cannot see your track). This DIVERGES from the
 * omnidirectional HOLD (spot-lock) law above, which may reverse for short holds.
 * Outside the +/-cone the bow is badly off target: thrust drops to a small
 * forward creep (a bow-mounted motor cannot pivot the hull with zero thrust) so
 * the boat turns toward the target; inside the cone thrust ramps up to full as
 * the bow lines up. Servo steers the SHORTEST way to face the target. */
#define SPOT_LOCK_GOTO_ALIGN_CONE_DEG10 300 /* +/-30 deg go-forward cone */
#define SPOT_LOCK_GOTO_TURN_CREEP 0.15      /* min forward thrust while turning */

#define DEG10_FULL_TURN 3600
#define DEG10_HALF_TURN 1800
#define DEG10_PER_DEG 10.0

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

/** Neutral-actuator output (throttle neutral, servo center) for a sub-state. */
static spot_lock_outputs make_idle_output(spot_lock_substate substate)
{
    spot_lock_outputs out = {
        .substate = substate,
        .throttle_cmd = 0,
        .servo_cmd = 0,
        .err_m = 0,
        .bearing_deg10 = 0,
        .arrived = false,
    };
    return out;
}

/** Signed bow-to-target error in degrees * 10, wrapped into [-1800, 1800]. */
static int heading_error_deg10(uint16_t bearing_deg10, uint16_t heading_deg10)
{
    int diff = (int)bearing_deg10 - (int)heading_deg10;
    diff = ((diff % DEG10_FULL_TURN) + DEG10_FULL_TURN) % DEG10_FULL_TURN;
    if (diff > DEG10_HALF_TURN) {
        diff -= DEG10_FULL_TURN;
    }
    return diff;
}

/** Distance in metres rounded and saturated into the uint16 telemetry range. */
static uint16_t dist_to_err_m(float dist_m)
{
    long m = lround((double)dist_m);
    if (m < 0) {
        m = 0;
    }
    if (m > UINT16_MAX) {
        m = UINT16_MAX;
    }
    return (uint16_t)m;
}

/** P-servo: proportional to bearing error, clamped to the symmetric range. */
static int32_t servo_command(int err_deg10, uint16_t gain_per_deg)
{
    double cmd = (double)gain_per_deg * ((double)err_deg10 / DEG10_PER_DEG);
    return clamp_i32((int32_t)lround(cmd), -SPOT_LOCK_CMD_FULL_SCALE,
                     SPOT_LOCK_CMD_FULL_SCALE);
}

/** P-throttle MAGNITUDE: proportional to distance, capped to max (R7). The drive
 * direction (forward/reverse) and alignment taper are applied by the caller. */
static int32_t throttle_command(float dist_m, const spot_lock_params *p)
{
    double cmd = (double)p->throttle_gain_per_m * (double)dist_m;
    return clamp_i32((int32_t)lround(cmd), 0, (int32_t)p->max_throttle_norm);
}

/* goto cruise-decel: full goto_cruise_norm beyond the slowdown distance, then
 * linear down to the deadband edge. Chain bypasses its power limit for
 * spot-lock/goto, so the cap is applied here (goto_cruise_norm = max_throttle_fwd_pct). */
static int32_t goto_throttle_command(float dist_m, const spot_lock_params *p)
{
    int32_t cruise = (int32_t)p->goto_cruise_norm;
    float deadband = (float)p->deadband_m;
    float slowdown = (float)p->goto_slowdown_distance_m;
    if (dist_m >= slowdown) {
        return cruise;
    }
    float span = slowdown - deadband;
    if (span <= 0.0f) {          /* misconfig: slowdown <= deadband -> no ramp zone */
        return cruise;
    }
    float frac = (dist_m - deadband) / span;   /* 0 at deadband edge, 1 at slowdown */
    return clamp_i32((int32_t)lround((double)cruise * (double)frac), 0, cruise);
}

/* Reduce the bow-to-target error to a drive command: choose forward or reverse
 * (whichever needs the hull to swing less) so the steering error handed to the
 * servo is always within +/-90 deg. Returns the reduced steering error (deg*10)
 * and sets *dir to +1 (forward) or -1 (reverse). A target dead astern reverses
 * straight back (steering error 0). */
static int reduce_to_drive(int err_deg10, int *dir)
{
    if (err_deg10 > SPOT_LOCK_REVERSE_SPLIT_DEG10) {
        *dir = -1;
        return err_deg10 - DEG10_HALF_TURN; /* (-900, 0] */
    }
    if (err_deg10 < -SPOT_LOCK_REVERSE_SPLIT_DEG10) {
        *dir = -1;
        return err_deg10 + DEG10_HALF_TURN; /* [0, 900) */
    }
    *dir = 1;
    return err_deg10;
}

/* Alignment taper factor in [ALIGN_FLOOR, 1]: cos of the steering error, floored
 * so thrust never fully vanishes (no dead zone). steer_err_deg10 is within
 * +/-900 (reduce_to_drive guarantees it), so cos is within [0, 1]. */
static double align_factor(int steer_err_deg10)
{
    double c = cos((double)steer_err_deg10 * SPOT_LOCK_DEG10_TO_RAD);
    return (c < SPOT_LOCK_ALIGN_FLOOR) ? SPOT_LOCK_ALIGN_FLOOR : c;
}

/* GOTO forward-only alignment factor, always >= 0 (GOTO never reverses). Outside
 * the +/-cone: a small creep so the boat pivots the bow toward the target without
 * driving forward hard; inside the cone: linear ramp from that creep up to 1.0 as
 * the bow lines up. err_deg10 is within +/-1800 (full-turn error). */
static double goto_align_factor(int err_deg10)
{
    int a = (err_deg10 < 0) ? -err_deg10 : err_deg10;
    if (a >= SPOT_LOCK_GOTO_ALIGN_CONE_DEG10) {
        return SPOT_LOCK_GOTO_TURN_CREEP;
    }
    double frac = 1.0 - (double)a / (double)SPOT_LOCK_GOTO_ALIGN_CONE_DEG10;
    return SPOT_LOCK_GOTO_TURN_CREEP + (1.0 - SPOT_LOCK_GOTO_TURN_CREEP) * frac;
}

/* GOTO drive: forward-only, turn the bow to the target first, then go. Servo
 * steers by the full bearing error (shortest way to face the target); throttle
 * is the goto cruise profile scaled by goto_align_factor -- never reverse. */
static void apply_goto_drive(spot_lock_outputs *out, int err_deg10, float dist,
                             const spot_lock_params *p)
{
    out->servo_cmd = servo_command(err_deg10, p->servo_gain_per_deg);
    int32_t magnitude = goto_throttle_command(dist, p);
    out->throttle_cmd = (int32_t)lround((double)magnitude *
                                        goto_align_factor(err_deg10));
}

/* HOLD (spot-lock) drive: omnidirectional -- forward or reverse, whichever swings
 * the hull less -- with a cos alignment taper. Reverse is acceptable for short
 * holding corrections (NOT for GOTO). */
static void apply_hold_drive(spot_lock_outputs *out, int err_deg10, float dist,
                             const spot_lock_params *p)
{
    int dir;
    int steer_err = reduce_to_drive(err_deg10, &dir);
    out->servo_cmd = servo_command(steer_err, p->servo_gain_per_deg);
    int32_t magnitude = throttle_command(dist, p);
    out->throttle_cmd = (int32_t)lround((double)dir * (double)magnitude *
                                        align_factor(steer_err));
}

/** Full ACTIVE-state regulator: deadband relax, then drive toward the target.
 * The drive law depends on the source: GOTO turns the bow to the target and
 * drives FORWARD only (never reverse -- see apply_goto_drive); HOLD (spot-lock)
 * is omnidirectional and may reverse for short holds (apply_hold_drive). No
 * forward-thrust gate either way, so the hull can never stall unable to rotate. */
static spot_lock_outputs compute_active_output(const spot_lock_inputs *in,
                                               const spot_lock_params *p,
                                               const spot_lock_state *st)
{
    geo_offset off = geo_offset_m(st->ref_lat_e7, st->ref_lon_e7, in->lat_e7,
                                  in->lon_e7);
    float dist = geo_distance_m(off);

    spot_lock_outputs out = {
        .substate = SPOT_LOCK_ACTIVE,
        .throttle_cmd = 0,
        .servo_cmd = 0,
        .err_m = dist_to_err_m(dist),
        .bearing_deg10 = geo_bearing_deg10(off),
        .arrived = false,
    };
    out.arrived = (out.err_m <= p->deadband_m);

    /* Deadband: inside the hold radius we relax (no heading hold, R6). */
    if (dist <= (float)p->deadband_m) {
        return out;
    }

    int err_deg10 = heading_error_deg10(out.bearing_deg10, in->heading_deg10);

    /* Drive law by source: GOTO turns-then-goes forward only; HOLD is
     * omnidirectional (may reverse). Both steer toward the target, no gate. */
    if (st->target_source == SPOT_LOCK_SRC_GOTO) {
        apply_goto_drive(&out, err_deg10, dist, p);
    } else {
        apply_hold_drive(&out, err_deg10, dist, p);
    }
    return out;
}

/** Force the OFF sub-state with no engaged source. */
static spot_lock_outputs make_off(spot_lock_state *st)
{
    st->substate = SPOT_LOCK_OFF;
    st->target_source = SPOT_LOCK_SRC_NONE;
    return make_idle_output(SPOT_LOCK_OFF);
}

/**
 * Pause-or-run for an engaged source with the target already set in st.
 * The fix is re-validated each cycle: a stale fix can still be inside the
 * freshness window (seed-fresh), so holding on a lost fix is unsafe. Pausing is
 * driven SOLELY by the SENSOR degradation domain (GPS/IMU): the app link never
 * pauses a latched intent (R3/R4), the RC (stick override / CH3 preempt / disarm)
 * is the sole failsafe.
 */
static spot_lock_outputs hold_or_pause(const spot_lock_inputs *in,
                                       const spot_lock_params *p,
                                       spot_lock_state *st)
{
    bool sensors_lost = !in->gps_fresh || !in->imu_ok || !in->gps_has_fix;
    if (sensors_lost) {
        st->substate = SPOT_LOCK_PAUSED;
        return make_idle_output(SPOT_LOCK_PAUSED);
    }
    st->substate = SPOT_LOCK_ACTIVE;
    return compute_active_output(in, p, st);
}

/**
 * CH3 hold branch (SRC_HOLD). On transition into hold - a fresh CH3 request or
 * preempting a running goto - snapshot "here and now" as the target; this
 * requires a rising edge and a real, fresh fix. A held CH3 keeps its existing
 * snapshot. Link freshness never gates SRC_HOLD.
 */
static spot_lock_outputs run_ch3_hold(const spot_lock_inputs *in,
                                      const spot_lock_params *p,
                                      spot_lock_state *st)
{
    if (st->target_source != SPOT_LOCK_SRC_HOLD) {
        if (!in->ch3_edge_on || !in->gps_fresh || !in->gps_has_fix) {
            return make_off(st);
        }
        st->ref_lat_e7 = in->lat_e7;
        st->ref_lon_e7 = in->lon_e7;
        st->target_source = SPOT_LOCK_SRC_HOLD;
    }
    return hold_or_pause(in, p, st);
}

spot_lock_outputs spot_lock_step(const spot_lock_inputs *in,
                                 const spot_lock_params *p,
                                 spot_lock_state *st)
{
    /* 1. Manual override / disarm wins unconditionally (R4/R6). */
    if (!in->armed || !in->sticks_neutral) {
        return make_off(st);
    }

    /* 2. CH3 physically preempts goto: hold "here and now" (R4). */
    if (in->ch3_on) {
        return run_ch3_hold(in, p, st);
    }

    /* 3. App-driven goto: a latched external target that PERSISTS across app-link
     * loss (R3/R4) - a stale link never pauses it (hold_or_pause has no link gate);
     * the RC (stick override / CH3 preempt / disarm) is the sole failsafe. comms_fresh
     * is NOT a link failsafe here: it is the retarget-in-flight / re-latch gate.
     * The reference is (re)latched from the input ONLY on entry into SRC_GOTO or
     * while the link is fresh (R1: a fresh link tracks a newly commanded goto
     * point). While the link is stale (comms_fresh == false) the core RETAINS the
     * last good target and does NOT overwrite ref_* from the input - so retention
     * across a link gap is a property of this pure core, not an implicit contract
     * on the upstream latch (guards null-island if the loop zeroes goto_* on link
     * loss). Only the SENSOR domain (GPS/IMU, in hold_or_pause) pauses goto. */
    if (in->goto_engage) {
        bool is_entering_goto = st->target_source != SPOT_LOCK_SRC_GOTO;
        if (is_entering_goto || in->comms_fresh) {
            st->ref_lat_e7 = in->goto_lat_e7;
            st->ref_lon_e7 = in->goto_lon_e7;
        }
        st->target_source = SPOT_LOCK_SRC_GOTO;
        return hold_or_pause(in, p, st);
    }

    /* 4. No source engaged. */
    return make_off(st);
}
