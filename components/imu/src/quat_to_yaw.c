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
 * Y=North). Compass heading is measured CW from North (world Y). Converting
 * between a CCW-from-East angle and a CW-from-North angle is a reflection
 * around the 45 deg axis (equidistant from East and North), not a plain
 * negation around 0/north -- confirmed on hardware: rotating the hull left
 * showed up as a RIGHT turn in the raw yaw, and the one static heading that
 * happened to read correctly was near 56 deg (close to this 45 deg axis, the
 * gap being plausible mounting/measurement offset), not near 0/180. */
#define COMPASS_MIRROR_AXIS_DEG10 450

uint16_t yaw_to_compass_heading_deg10(uint16_t yaw_deg10)
{
    int deg10 = ((COMPASS_MIRROR_AXIS_DEG10 * 2 - (int)yaw_deg10) % DEG10_FULL_TURN +
                DEG10_FULL_TURN) %
               DEG10_FULL_TURN;
    return (uint16_t)deg10;
}
