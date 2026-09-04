#include "quat_to_yaw.h"

#include <math.h>

/* BNO085 Rotation Vector quaternion fixed-point scaling: each int16 component
 * is the real value times 2^14. */
#define QUAT_Q14_SCALE 16384.0

#define DEG_PER_RAD (180.0 / M_PI)
#define DEG_FULL_TURN 360.0
#define DEG10_PER_DEG 10.0
#define DEG10_FULL_TURN 3600

uint16_t quat_to_yaw_deg10(int16_t q_i, int16_t q_j, int16_t q_k,
                           int16_t q_real)
{
    /* Normalised quaternion components (dividing by a common positive scale
     * does not change atan2's ratio, but keeps the magnitudes physical). */
    double i = (double)q_i / QUAT_Q14_SCALE;
    double j = (double)q_j / QUAT_Q14_SCALE;
    double k = (double)q_k / QUAT_Q14_SCALE;
    double w = (double)q_real / QUAT_Q14_SCALE;

    double siny_cosp = 2.0 * (w * k + i * j);
    double cosy_cosp = 1.0 - 2.0 * (j * j + k * k);
    double yaw_rad = atan2(siny_cosp, cosy_cosp);

    double yaw_deg = yaw_rad * DEG_PER_RAD;
    if (yaw_deg < 0.0) {
        yaw_deg += DEG_FULL_TURN; /* wrap atan2's [-180,180) into [0,360) */
    }

    int deg10 = (int)lround(yaw_deg * DEG10_PER_DEG);
    /* Guard the rounding boundary at 360.0 deg so the result stays in range. */
    if (deg10 >= DEG10_FULL_TURN) {
        deg10 -= DEG10_FULL_TURN;
    }
    if (deg10 < 0) {
        deg10 += DEG10_FULL_TURN;
    }
    return (uint16_t)deg10;
}

/* Reflection axis, degrees * 10: yaw_math is measured CCW from the world X
 * axis (East, per the BNO085/Android Rotation Vector ENU convention: X=East,
 * Y=North, Z=Up -- identity mapping per CEVA BNO08X Datasheet v1.17 sec 4).
 * Compass heading is measured CW from North (world Y). Converting between a
 * CCW-from-East angle and a CW-from-North angle is a reflection around the
 * 45 deg axis (equidistant from East and North), not a plain negation
 * around 0/north -- confirmed on hardware: rotating the hull left showed up
 * as a RIGHT turn in the raw yaw.
 *
 * No mounting-derived nudge beyond the theoretical 45 deg: the mount is
 * confirmed clean. The X arrow points to the bow within ~1 deg, and a bench
 * check on 2026-09-04 (raw Rotation Vector quaternion logged flat, axis-
 * diagram side up, via the ESP_LOGI in bno085.c: i~=-0.94, j~=-0.34,
 * k~=-0.015, w~=0.02) showed the sensor's Y/Z axes are physically inverted
 * relative to the BNO08X datasheet reference (Figure 4-1) -- but decomposed
 * as roll/pitch/yaw (ZYX Euler) that reading is roll~=-178 deg, pitch~=-2.5
 * deg, yaw~=40-44 deg: a rotation almost entirely about the sensor's OWN X
 * axis. quat_to_yaw_deg10's yaw output depends only on yaw and pitch, never
 * roll (rotating about X cannot move X), so this near-180 deg roll is
 * mathematically invisible to it -- it does NOT need compensating here, and
 * folding it into this axis constant was the earlier, wrong model.
 * Physically re-mounting is not an option right now (the IMU is installed
 * in the motor housing with wires connected) -- but per the above, none is
 * needed for yaw/heading correctness.
 *
 * A single on-water compass comparison (2026-09-03, imu_calib=3/3 at the
 * time) measured a 6.3 deg deficit (telemetry 61.8 vs a real compass 55.5)
 * and that was mistakenly folded in here as a "mounting offset" (giving
 * 50.4 deg) -- reverted. With the mount confirmed clean, that deficit isn't
 * geometric: the same 2026-09-04 bench log showed yaw drifting ~4 deg over
 * 3 stationary minutes while roll/pitch held rock-steady, pointing at
 * mag-calibration convergence (not yet settled at measurement time) rather
 * than a fixed offset -- and even if it were, a flat correction fit to ONE
 * heading would only be correct at that heading if the true cause is
 * heading-dependent magnetic deviation (hard/soft iron from the nearby
 * motor/battery), which this axis constant cannot model. Investigate that
 * deficit on its own terms (let calib fully converge before comparing;
 * check with motor on vs off) rather than re-baking it in here. See
 * docs/dev-brainstorms/2026-09-04-imu-mount-offset-requirements.md. */
#define COMPASS_MIRROR_AXIS_DEG10 450

uint16_t yaw_to_compass_heading_deg10(uint16_t yaw_deg10)
{
    int deg10 = ((COMPASS_MIRROR_AXIS_DEG10 * 2 - (int)yaw_deg10) % DEG10_FULL_TURN +
                DEG10_FULL_TURN) %
               DEG10_FULL_TURN;
    return (uint16_t)deg10;
}
