#include "spot_lock.h"

#include <math.h>
#include <stdint.h>

#include "geo_math.h"

/* Forward-thrust gate: thrust is applied only while the bow points within
 * +/-60 deg of the target bearing (R2). Expressed in degrees * 10. */
#define SPOT_LOCK_HEADING_GATE_DEG10 600

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

/** P-throttle: proportional to distance, forward-only, capped to max (R7). */
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

/** Full ACTIVE-state regulator: deadband, +/-60 gate, P servo + P throttle. */
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
    out.servo_cmd = servo_command(err_deg10, p->servo_gain_per_deg);

    /* Forward thrust only inside the +/-60 deg gate (R2); a target astern gets
     * pure steering and crawls round in one gentle turn. */
    if (err_deg10 >= -SPOT_LOCK_HEADING_GATE_DEG10 &&
        err_deg10 <= SPOT_LOCK_HEADING_GATE_DEG10) {
        out.throttle_cmd = (st->target_source == SPOT_LOCK_SRC_GOTO)
                               ? goto_throttle_command(dist, p)
                               : throttle_command(dist, p);
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
 * freshness window (seed-fresh), so holding on a lost fix is unsafe. This SENSOR
 * degradation domain (GPS/IMU) is separate from the app link: comms_gated maps a
 * source onto the (now non-pausing) link domain. Both sources pass comms_gated =
 * false - a stale app link never pauses a latched intent (R3/R4), the RC is the
 * sole failsafe. The parameter is kept so the two degradation domains stay
 * structurally distinct at the call sites.
 */
static spot_lock_outputs hold_or_pause(const spot_lock_inputs *in,
                                       const spot_lock_params *p,
                                       spot_lock_state *st, bool comms_gated)
{
    bool sensors_lost = !in->gps_fresh || !in->imu_ok || !in->gps_has_fix;
    if (comms_gated && !in->comms_fresh) {
        sensors_lost = true;
    }
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
    return hold_or_pause(in, p, st, false);
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
     * loss (R3/R4) - a stale link never pauses it (comms_gated = false below); the
     * RC (stick override / CH3 preempt / disarm) is the sole failsafe. comms_fresh
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
        return hold_or_pause(in, p, st, false);
    }

    /* 4. No source engaged. */
    return make_off(st);
}
