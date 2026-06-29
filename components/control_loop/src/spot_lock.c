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
    };

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
        out.throttle_cmd = throttle_command(dist, p);
    }
    return out;
}

spot_lock_outputs spot_lock_step(const spot_lock_inputs *in,
                                 const spot_lock_params *p,
                                 spot_lock_state *st)
{
    /* 1. Abort to OFF from any sub-state: disarm, CH3 off, or stick moved (R4). */
    if (!in->armed || !in->ch3_on || !in->sticks_neutral) {
        st->substate = SPOT_LOCK_OFF;
        return make_idle_output(SPOT_LOCK_OFF);
    }

    /* 2. Entry OFF -> ACTIVE: rising edge with a real, fresh fix (R1/R3). The
     * real-fix gate prevents the seed-fresh window from arming without a fix. */
    if (st->substate == SPOT_LOCK_OFF) {
        if (!in->ch3_edge_on || !in->gps_fresh || !in->gps_has_fix) {
            return make_idle_output(SPOT_LOCK_OFF);
        }
        st->ref_lat_e7 = in->lat_e7;
        st->ref_lon_e7 = in->lon_e7;
        st->substate = SPOT_LOCK_ACTIVE;
    }

    /* 3. Pause on sensor loss; retain the target and relax actuators (R5). */
    if (!in->gps_fresh || !in->imu_ok) {
        st->substate = SPOT_LOCK_PAUSED;
        return make_idle_output(SPOT_LOCK_PAUSED);
    }

    /* 4. Hold position. */
    st->substate = SPOT_LOCK_ACTIVE;
    return compute_active_output(in, p, st);
}
